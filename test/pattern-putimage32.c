/*
 * pattern-putimage32.c
 *
 * Mimics a depth-32 (ARGB8888) TrueColor window whose contents
 * are pushed with xcb_put_image, which are split into multiple
 * requests to stay under the protocol maximum request size, with
 * optional partial-update rects and an optional XShmPutImage path.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>

struct args {
    int width;
    int height;
    int fps;
    int fullscreen;
    int partial;
    int shm;
};

struct buffer {
    uint32_t *full;  /* width*height ARGB pixels */
    uint32_t *rect;  /* up to width*height, holds just the update rect */
    uint8_t *shm_data;
    int shmid;
    xcb_shm_seg_t seg;
};

static void
usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--size WxH] [--fps N]\n"
            "       [--fullscreen] [--partial] [--shm]\n"
            "  --size WxH      window size (default 1024x768)\n"
            "  --fps N         frame rate (default 60)\n"
            "  --fullscreen    override-redirect full-screen window\n"
            "  --partial       update only a moving sub-rect (dst_x/dst_y)\n"
            "  --shm           use XShmPutImage instead of xcb_put_image\n",
            prog);
}

static int
parse_args(int argc, char **argv, struct args *args)
{
    args->width = 1024;
    args->height = 768;
    args->fps = 60;
    args->fullscreen = 0;
    args->partial = 0;
    args->shm = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &args->width, &args->height) != 2) {
                fprintf(stderr, "bad --size %s\n", argv[i]);
                return -1;
            }
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            args->fps = atoi(argv[++i]);
            if (args->fps <= 0) {
                fprintf(stderr, "bad --fps %s\n", argv[i]);
                return -1;
            }
        } else if (!strcmp(argv[i], "--fullscreen")) {
            args->fullscreen = 1;
        } else if (!strcmp(argv[i], "--partial")) {
            args->partial = 1;
        } else if (!strcmp(argv[i], "--shm")) {
            args->shm = 1;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 1;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

/* Find a depth-32 TrueColor visual */
static xcb_visualtype_t *
find_visual_32(xcb_screen_t *screen, xcb_visualid_t *visual_id)
{
    xcb_depth_iterator_t d_iter = xcb_screen_allowed_depths_iterator(screen);

    while (d_iter.rem) {
        xcb_depth_t *depth = d_iter.data;

        if (depth->depth == 32) {
            xcb_visualtype_iterator_t v_iter = xcb_depth_visuals_iterator(depth);

            while (v_iter.rem) {
                xcb_visualtype_t *vis = v_iter.data;

                if (vis->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
                    *visual_id = vis->visual_id;
                    return vis;
                }
                xcb_visualtype_next(&v_iter);
            }
        }
        xcb_depth_next(&d_iter);
    }
    return NULL;
}

static inline uint32_t
pack_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

static void
render_pattern(uint32_t *buf, int width, int height, int bar_y)
{
    for (int y = 0; y < height; y++) {
        uint32_t color = pack_pixel((uint8_t)(y * 255 / height),
                                    128,
                                    (uint8_t)(255 - y * 255 / height));
        uint32_t *row = buf + (size_t)y * width;

        for (int x = 0; x < width; x++)
            row[x] = color;

        if (y == bar_y) {
            for (int x = 0; x < width; x++)
                row[x] = 0xffffffff;
        }
    }
}

/* push the rect in chunks under the protocol max request size. */
static void
put_image_rows(xcb_connection_t *conn, xcb_window_t win, xcb_gcontext_t gc,
               uint8_t depth, int rect_w, int rect_h,
               int dst_x, int dst_y, const uint8_t *data)
{
    size_t row_bytes = (size_t)rect_w * 4;
    uint32_t max_bytes = xcb_get_maximum_request_length(conn) * 4;
    int rows_per_request = (max_bytes - 32) / row_bytes;

    if (rows_per_request < 1)
        rows_per_request = 1;

    for (int row = 0; row < rect_h; row += rows_per_request) {
        size_t n_rows = rect_h - row;

        if (n_rows > (size_t)rows_per_request)
            n_rows = rows_per_request;
        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, win, gc,
                      rect_w, n_rows, dst_x, dst_y + row, 0,
                      depth, n_rows * row_bytes, data + (size_t)row * row_bytes);
    }
}

static int
setup_shm(xcb_connection_t *conn, struct args *args, struct buffer *buf)
{
    size_t size = (size_t)args->width * args->height * 4;
    xcb_shm_query_version_reply_t *rep;

    rep = xcb_shm_query_version_reply(conn, xcb_shm_query_version(conn), NULL);
    if (!rep) {
        fprintf(stderr, "XShm extension not available\n");
        return -1;
    }
    free(rep);

    buf->shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (buf->shmid < 0) {
        fprintf(stderr, "shmget failed: %s\n", strerror(errno));
        return -1;
    }
    buf->shm_data = shmat(buf->shmid, NULL, 0);
    if (buf->shm_data == (void *)-1) {
        fprintf(stderr, "shmat failed: %s\n", strerror(errno));
        return -1;
    }

    buf->seg = xcb_generate_id(conn);
    xcb_shm_attach(conn, buf->seg, buf->shmid, 0);
    return 0;
}

int
main(int argc, char **argv)
{
    struct args args;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_visualtype_t *visual;
    xcb_visualid_t visual_id;
    xcb_window_t win;
    xcb_gcontext_t gc;
    struct buffer buf = { 0 };
    int bar_y = 0;
    int band_y = 0;
    int ret = parse_args(argc, argv, &args);

    if (ret == 1)
        return 0;
    if (ret < 0)
        return 1;

    conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) {
        fprintf(stderr, "failed to connect to X server\n");
        return 1;
    }

    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

    visual = find_visual_32(screen, &visual_id);
    if (!visual) {
        fprintf(stderr, "no depth-32 TrueColor visual found\n");
        return 1;
    }

    /* Windows on a non-default visual need a colormap created for it;
     * otherwise CreateWindow fails with BadMatch (dix/window.c). */
    xcb_colormap_t cmap = xcb_generate_id(conn);
    xcb_create_colormap(conn, XCB_COLORMAP_ALLOC_NONE, cmap, screen->root,
                        visual_id);

    if (args.fullscreen) {
        args.width = screen->width_in_pixels;
        args.height = screen->height_in_pixels;
    }

    win = xcb_generate_id(conn);
    /* A window whose depth differs from its parent (root, depth 24) must set
     * CWBorderPixel or CWBorderPixmap (dix/window.c), plus a colormap for its
     * non-default visual. Values are packed in ascending value_mask bit order:
     * bit1 BACK_PIXEL, bit3 BORDER_PIXEL, bit9 OVERRIDE_REDIRECT,
     * bit11 EVENT_MASK, bit12 COLORMAP. */
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[5];
    values[0] = screen->black_pixel; /* bit 1  BACK_PIXEL */
    values[1] = 0;                    /* bit 3  BORDER_PIXEL */
    if (args.fullscreen) {
        mask |= XCB_CW_OVERRIDE_REDIRECT; /* bit 9 */
        values[2] = 1;
        values[3] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY; /* bit 11 EVENT_MASK */
        values[4] = cmap; /* bit 12 COLORMAP */
    } else {
        values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY; /* bit 11 EVENT_MASK */
        values[3] = cmap; /* bit 12 COLORMAP */
    }
    mask |= XCB_CW_COLORMAP;
    {
        xcb_void_cookie_t cw_cookie =
            xcb_create_window(conn, 32, win, screen->root,
                              0, 0, args.width, args.height, 0,
                              XCB_WINDOW_CLASS_INPUT_OUTPUT, visual_id,
                              mask, values);
        xcb_generic_error_t *err = xcb_request_check(conn, cw_cookie);

        if (err) {
            fprintf(stderr, "create_window failed: error_code %d, resource_id 0x%x\n",
                    err->error_code, err->resource_id);
            free(err);
            return 1;
        }
    }

    gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, win, 0, NULL);

    fprintf(stderr, "visual 0x%x (depth 32, red_mask 0x%x), screen %dx%d, "
            "window %dx%d at 0,0\n",
            visual_id, visual->red_mask,
            screen->width_in_pixels, screen->height_in_pixels,
            args.width, args.height);

    xcb_map_window(conn, win);
    xcb_flush(conn);

    /* Skip the initial Expose map flicker, with a real timeout: poll instead
     * of blocking so zero events can't hang the app forever. */
    {
        struct timespec t0, t1;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (;;) {
            xcb_generic_event_t *ev = xcb_poll_for_event(conn);
            int got = 0;

            while (ev) {
                got = 1;
                if ((ev->response_type & 0x7f) == XCB_EXPOSE) {
                    free(ev);
                    goto exposed;
                }
                free(ev);
                ev = xcb_poll_for_event(conn);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (t1.tv_sec - t0.tv_sec > 2) {
                fprintf(stderr, "timed out waiting for Expose%s\n",
                        got ? " (events received)" : " (no events)");
                break;
            }
            usleep(1000);
        }
    }
exposed:

    buf.full = malloc((size_t)args.width * args.height * sizeof(uint32_t));
    buf.rect = malloc((size_t)args.width * args.height * sizeof(uint32_t));
    if (!buf.full || !buf.rect) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    if (args.shm && setup_shm(conn, &args, &buf) < 0)
        return 1;

    for (;;) {
        struct timespec ts;
        int rx = 0, ry = 0, rw = args.width, rh = args.height;

        render_pattern(buf.full, args.width, args.height, bar_y);

        if (args.partial) {
            rh = args.height / 8;
            if (rh < 1)
                rh = 1;
            ry = band_y;
            /* Bright green band so its position/dst offset is obvious. */
            for (int y = 0; y < rh; y++) {
                uint32_t *row = buf.full + (size_t)(ry + y) * args.width;
                for (int x = 0; x < rw; x++)
                    row[x] = 0xff00ff00;
            }
            band_y = (band_y + 1) % (args.height - rh + 1);
        }

        if (args.shm) {
            memcpy(buf.shm_data, buf.full,
                   (size_t)args.width * args.height * 4);
            xcb_shm_put_image(conn, win, gc,
                              args.width, args.height,
                              rx, ry, rw, rh, rx, ry,
                              32, XCB_IMAGE_FORMAT_Z_PIXMAP, 0,
                              buf.seg, 0);
        } else {
            /* Compact rect rows */
            for (int y = 0; y < rh; y++)
                memcpy(buf.rect + (size_t)y * rw,
                       buf.full + (size_t)(ry + y) * args.width,
                       (size_t)rw * sizeof(uint32_t));
            put_image_rows(conn, win, gc, 32, rw, rh, rx, ry,
                           (const uint8_t *)buf.rect);
        }
        xcb_flush(conn);

        bar_y = (bar_y + 1) % args.height;

        ts.tv_sec = 0;
        ts.tv_nsec = 1000000000 / args.fps;
        nanosleep(&ts, NULL);
    }

    return 0;
}