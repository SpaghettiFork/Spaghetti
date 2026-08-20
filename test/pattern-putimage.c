/*
 * pattern-putimage.c
 *
 * Blits a full-window test pattern into a window using plain xcb_put_image
 * 
 * The pattern is a per-row color gradient with a
 * moving horizontal white bar, so any missing/swizzled rows and frame
 * progression are easy to spot.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>

struct args {
    int width;
    int height;
    int fps;
};

static void
usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--size WxH] [--fps N]\n"
            "  --size WxH   window size (default 1024x768)\n"
            "  --fps N      frame rate (default 60)\n",
            prog);
}

static int
parse_args(int argc, char **argv, struct args *args)
{
    args->width = 1024;
    args->height = 768;
    args->fps = 60;

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
render_pattern(uint32_t *buf, int width, int height, int bar_y)
{
    for (int y = 0; y < height; y++) {
        /* Per-row color gradient so lost rows are obvious. */
        uint32_t color = pack_pixel((uint8_t)(y * 255 / height),
                                    128,
                                    (uint8_t)(255 - y * 255 / height));
        uint32_t *row = buf + (size_t)y * width;

        for (int x = 0; x < width; x++)
            row[x] = color;

        /* Moving white bar to show frame progression. */
        if (y == bar_y) {
            for (int x = 0; x < width; x++)
                row[x] = 0xffffffff;
        }
    }
}

int
main(int argc, char **argv)
{
    struct args args;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t win;
    xcb_gcontext_t gc;
    uint32_t *buf;
    int bar_y = 0;
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

    win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        screen->black_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
    };
    xcb_create_window(conn, screen->root_depth, win, screen->root,
                      0, 0, args.width, args.height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      mask, values);

    gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, win, 0, NULL);

    xcb_map_window(conn, win);
    xcb_flush(conn);

    /* Skip the initial Expose map flicker. */
    {
        xcb_generic_event_t *ev;
        while ((ev = xcb_wait_for_event(conn))) {
            if (ev->response_type == XCB_EXPOSE)
                break;
            free(ev);
        }
        free(ev);
    }

    buf = malloc((size_t)args.width * args.height * 4);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    for (;;) {
        struct timespec ts;

        render_pattern(buf, args.width, args.height, bar_y);

        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, win, gc,
                      args.width, args.height, 0, 0, 0,
                      screen->root_depth,
                      (size_t)args.width * args.height * 4,
                      (const uint8_t *)buf);
        xcb_flush(conn);

        bar_y = (bar_y + 1) % args.height;

        ts.tv_sec = 0;
        ts.tv_nsec = 1000000000 / args.fps;
        nanosleep(&ts, NULL);
    }

    free(buf);
    xcb_disconnect(conn);
    return 0;
}