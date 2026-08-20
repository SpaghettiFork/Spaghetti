/*
 * pattern-present.c
 *
 * Exercises the Present extension copy path that DRI3 GL clients use:
 * create a pixmap, export it via xcb_dri3_buffer_from_pixmap to get a
 * PRIME fd, mmap the fd, fill it with a per-row color gradient plus a
 * moving horizontal white bar (like pattern-putimage.c), then present it
 * windowed or fullscreen via xcb_present_pixmap.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/sync.h>

struct args {
    int width;
    int height;
    int fps;
    int num_buffers;
    int fullscreen;
    int no_wait_fence;
    int copy;
};

struct buffer {
    xcb_pixmap_t pixmap;
    xcb_sync_fence_t idle_fence;
    uint8_t *data;
    uint32_t size;
    uint16_t stride;
};

static void
usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--size WxH] [--fps N] [--buffers N]\n"
            "       [--fullscreen] [--no-wait-fence] [--copy]\n"
            "  --size WxH       window size (default 1024x768)\n"
            "  --fps N          frame rate (default 60)\n"
            "  --buffers N      number of rotating buffers (default 3)\n"
            "  --fullscreen     override-redirect full-screen window\n"
            "  --no-wait-fence  pass no wait fence (copy happens immediately)\n"
            "  --copy           force PresentOptionCopy (prevent page-flip)\n",
            prog);
}

static int
parse_args(int argc, char **argv, struct args *args)
{
    args->width = 1024;
    args->height = 768;
    args->fps = 60;
    args->num_buffers = 3;
    args->fullscreen = 0;
    args->no_wait_fence = 0;
    args->copy = 0;

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
        } else if (!strcmp(argv[i], "--buffers") && i + 1 < argc) {
            args->num_buffers = atoi(argv[++i]);
            if (args->num_buffers < 1) {
                fprintf(stderr, "bad --buffers %s\n", argv[i]);
                return -1;
            }
        } else if (!strcmp(argv[i], "--fullscreen")) {
            args->fullscreen = 1;
        } else if (!strcmp(argv[i], "--no-wait-fence")) {
            args->no_wait_fence = 1;
        } else if (!strcmp(argv[i], "--copy")) {
            args->copy = 1;
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

static inline uint32_t
pack_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

static void
render_pattern(struct buffer *buf, int width, int height, int bar_y)
{
    for (int y = 0; y < height; y++) {
        uint32_t color = pack_pixel((uint8_t)(y * 255 / height),
                                    128,
                                    (uint8_t)(255 - y * 255 / height));
        uint32_t *row = (uint32_t *)(buf->data + (size_t)y * buf->stride);

        for (int x = 0; x < width; x++)
            row[x] = color;

        if (y == bar_y) {
            for (int x = 0; x < width; x++)
                row[x] = 0xffffffff;
        }
    }
}

static int
create_buffer(xcb_connection_t *conn, xcb_window_t win, uint8_t depth,
              struct args *args, int index, struct buffer *buf)
{
    xcb_generic_error_t *err = NULL;
    xcb_dri3_buffer_from_pixmap_reply_t *reply;
    int *fds;
    int fd;

    buf->pixmap = xcb_generate_id(conn);
    xcb_create_pixmap(conn, depth, buf->pixmap, win,
                      args->width, args->height);

    reply = xcb_dri3_buffer_from_pixmap_reply(conn,
                xcb_dri3_buffer_from_pixmap(conn, buf->pixmap), &err);
    if (err) {
        fprintf(stderr, "buffer_from_pixmap failed for buffer %d: "
                "error_code %d, resource_id 0x%x\n",
                index, err->error_code, err->resource_id);
        free(err);
        return -1;
    }
    if (!reply) {
        fprintf(stderr, "buffer_from_pixmap returned no reply for buffer %d\n", index);
        return -1;
    }

    fds = xcb_dri3_buffer_from_pixmap_reply_fds(conn, reply);
    if (!fds || reply->nfd < 1) {
        fprintf(stderr, "no fd from buffer_from_pixmap for buffer %d\n", index);
        free(reply);
        return -1;
    }
    fd = fds[0];
    buf->stride = reply->stride;
    buf->size = reply->size;

    buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf->data == MAP_FAILED) {
        fprintf(stderr, "mmap failed for buffer %d (%s)\n", index, strerror(errno));
        close(fd);
        free(reply);
        return -1;
    }
    close(fd);
    free(reply);

    /* Idle fence: initially triggered so the first present can proceed. */
    buf->idle_fence = xcb_generate_id(conn);
    xcb_sync_create_fence(conn, win, buf->idle_fence, 1);

    return 0;
}

static int
wait_fence_triggered(xcb_connection_t *conn, xcb_sync_fence_t fence)
{
    xcb_sync_query_fence_reply_t *reply;

    reply = xcb_sync_query_fence_reply(conn,
                xcb_sync_query_fence(conn, fence), NULL);
    if (!reply)
        return 0;
    int triggered = reply->triggered;
    free(reply);
    return triggered;
}

int
main(int argc, char **argv)
{
    struct args args;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t win;
    xcb_sync_fence_t wait_fence = 0;
    struct buffer *buffers;
    int bar_y = 0;
    uint32_t serial = 0;
    int cur = 0;
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

    if (args.fullscreen) {
        args.width = screen->width_in_pixels;
        args.height = screen->height_in_pixels;
    }

    win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[3];
    values[0] = screen->black_pixel; /* bit 1  BACK_PIXEL */
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY; /* bit 11 EVENT_MASK */
    if (args.fullscreen) {
        mask |= XCB_CW_OVERRIDE_REDIRECT; /* bit 9 */
        values[2] = values[1];
        values[1] = 1;
    }
    xcb_create_window(conn, screen->root_depth, win, screen->root,
                      0, 0, args.width, args.height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      mask, values);

    xcb_map_window(conn, win);
    xcb_flush(conn);

    /* Skip the initial Expose map flicker, but don't hang forever. */
    {
        xcb_generic_event_t *ev;
        struct timespec t0, t1;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        while ((ev = xcb_wait_for_event(conn))) {
            if ((ev->response_type & 0x7f) == XCB_EXPOSE)
                break;
            free(ev);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (t1.tv_sec - t0.tv_sec > 2) {
                fprintf(stderr, "timed out waiting for Expose\n");
                break;
            }
        }
        free(ev);
    }

    fprintf(stderr, "screen %dx%d, window %dx%d at 0,0\n",
            screen->width_in_pixels, screen->height_in_pixels,
            args.width, args.height);

    buffers = calloc(args.num_buffers, sizeof(struct buffer));
    if (!buffers) {
        fprintf(stderr, "calloc failed\n");
        return 1;
    }
    for (int i = 0; i < args.num_buffers; i++) {
        if (create_buffer(conn, win, screen->root_depth, &args, i, &buffers[i]) < 0)
            return 1;
    }

    if (!args.no_wait_fence) {
        wait_fence = xcb_generate_id(conn);
        /* Initially triggered: the buffer is already CPU-filled. */
        xcb_sync_create_fence(conn, win, wait_fence, 1);
    }

    for (;;) {
        struct buffer *buf = &buffers[cur];
        struct timespec ts;

        /* Wait until this buffer is no longer being scanned out. */
        while (!wait_fence_triggered(conn, buf->idle_fence))
            usleep(1000);

        render_pattern(buf, args.width, args.height, bar_y);

        xcb_present_pixmap(conn, win, buf->pixmap, ++serial,
                           0, 0, 0, 0, 0,
                           wait_fence, buf->idle_fence,
                           args.copy ? XCB_PRESENT_OPTION_COPY : 0,
                           0, 0, 0, 0, NULL);
        xcb_flush(conn);

        cur = (cur + 1) % args.num_buffers;
        bar_y = (bar_y + 1) % args.height;

        ts.tv_sec = 0;
        ts.tv_nsec = 1000000000 / args.fps;
        nanosleep(&ts, NULL);
    }

    return 0;
}