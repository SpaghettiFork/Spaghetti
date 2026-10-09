/*
 * Compares Composite throughput with alphaMaps attached (glamor GPU path
 * vs fbComposite CPU fallback) and verifies pixel equality between runs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>

/* tunables, overridable on the command line */
static int opt_batches = 10;
static int opt_per_batch = 20;
static int opt_warmup = 30;
static int opt_maxsize;
static const char *opt_dumpdir;

static int xerr_code;

static int
xerr_handler(Display *dpy, XErrorEvent *ev)
{
    (void) dpy;
    xerr_code = ev->error_code;
    return 0;
}

static unsigned long long
now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long) ts.tv_sec * 1000000ULL +
        (unsigned long long) ts.tv_nsec / 1000ULL;
}

/* FNV-1a 64 over raw image bytes */
static uint64_t
checksum_image(XImage *img)
{
    uint64_t h = 1469598103934665603ULL;
    int y;

    for (y = 0; y < img->height; y++) {
        unsigned char *row =
            (unsigned char *) img->data + y * img->bytes_per_line;
        int x;

        for (x = 0; x < img->bytes_per_line; x++) {
            h ^= row[x];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

static const XRenderPictFormat *
find_x2r10(Display *dpy)
{
    XRenderPictFormat templ;
    const XRenderPictFormat *fmt;
    int n = 0;

    memset(&templ, 0, sizeof(templ));
    templ.type = PictTypeDirect;
    templ.depth = 30;
    fmt = XRenderFindFormat(dpy,
                            PictFormatType | PictFormatDepth, &templ, n);
    if (!fmt)
        return NULL;
    if (fmt->direct.redMask != 0x3ff ||
        fmt->direct.greenMask != 0x3ff ||
        fmt->direct.blueMask != 0x3ff ||
        fmt->direct.alphaMask != 0)
        return NULL;
    return fmt;
}

struct case_resources {
    Pixmap src_pm;
    Pixmap dst_pm;
    Pixmap map_pm;
    Pixmap msk_pm;
    Picture src;
    Picture dst;
    Picture map;
    Picture msk;
    GC gc;
    GC gc8;
};

static void
free_case(Display *dpy, struct case_resources *r)
{
    if (r->src)
        XRenderFreePicture(dpy, r->src);
    if (r->dst)
        XRenderFreePicture(dpy, r->dst);
    if (r->map)
        XRenderFreePicture(dpy, r->map);
    if (r->msk)
        XRenderFreePicture(dpy, r->msk);
    if (r->gc)
        XFreeGC(dpy, r->gc);
    if (r->gc8)
        XFreeGC(dpy, r->gc8);
    if (r->src_pm)
        XFreePixmap(dpy, r->src_pm);
    if (r->dst_pm)
        XFreePixmap(dpy, r->dst_pm);
    if (r->map_pm)
        XFreePixmap(dpy, r->map_pm);
    if (r->msk_pm)
        XFreePixmap(dpy, r->msk_pm);
    memset(r, 0, sizeof(*r));
}

static void
fill_half(Display *dpy, Drawable d, GC gc, int w, int h,
          unsigned long left, unsigned long right)
{
    XSetForeground(dpy, gc, left);
    XFillRectangle(dpy, d, gc, 0, 0, w / 2, h);
    XSetForeground(dpy, gc, right);
    XFillRectangle(dpy, d, gc, w / 2, 0, w - w / 2, h);
}

/*
 * Build src/dst/map pictures for one (format, size) group. Returns
 * False (after XSync checkpoint) when the server cannot provide the
 * depth, in which case the caller skips the group. The GC must match
 * its drawable depth, hence one GC per depth.
 */
static Bool
setup_group(Display *dpy, Drawable root,
            const XRenderPictFormat *srcfmt, int srcdepth,
            const XRenderPictFormat *a8fmt,
            const XRenderPictFormat *argbfmt,
            int size, unsigned long src_l, unsigned long src_r,
            unsigned long map_l, unsigned long map_r,
            struct case_resources *r)
{
    XRenderPictureAttributes pa;

    memset(r, 0, sizeof(*r));
    memset(&pa, 0, sizeof(pa));
    xerr_code = 0;
    r->src_pm = XCreatePixmap(dpy, root, size, size, srcdepth);
    r->dst_pm = XCreatePixmap(dpy, root, size, size, srcdepth);
    r->map_pm = XCreatePixmap(dpy, root, size, size, 8);
    r->msk_pm = XCreatePixmap(dpy, root, 8, 8, 32);
    r->gc = XCreateGC(dpy, r->src_pm, 0, NULL);
    r->gc8 = XCreateGC(dpy, r->map_pm, 0, NULL);
    XSync(dpy, False);
    if (xerr_code || !r->src_pm || !r->dst_pm || !r->map_pm || !r->gc ||
        !r->gc8) {
        free_case(dpy, r);
        return False;
    }
    r->src = XRenderCreatePicture(dpy, r->src_pm, srcfmt, 0, &pa);
    r->dst = XRenderCreatePicture(dpy, r->dst_pm, srcfmt, 0, &pa);
    r->map = XRenderCreatePicture(dpy, r->map_pm, a8fmt, 0, &pa);
    pa.repeat = True;
    r->msk = XRenderCreatePicture(dpy, r->msk_pm, argbfmt, CPRepeat, &pa);
    XSync(dpy, False);
    if (xerr_code || !r->src || !r->dst || !r->map || !r->msk) {
        free_case(dpy, r);
        return False;
    }
    fill_half(dpy, r->src_pm, r->gc, size, size, src_l, src_r);
    fill_half(dpy, r->map_pm, r->gc8, size, size, map_l, map_r);
    XSetForeground(dpy, r->gc, src_l >> 3);
    XFillRectangle(dpy, r->dst_pm, r->gc, 0, 0, size, size);
    XSetForeground(dpy, r->gc, 0xffffffff);
    XFillRectangle(dpy, r->msk_pm, r->gc, 0, 0, 8, 8);
    XSync(dpy, False);
    if (xerr_code) {
        free_case(dpy, r);
        return False;
    }
    return True;
}

/* dst accumulates across cases, so reset it for isolated measurements */
static void
reset_dst(Display *dpy, struct case_resources *r, int size,
          unsigned long px)
{
    XSetForeground(dpy, r->gc, px);
    XFillRectangle(dpy, r->dst_pm, r->gc, 0, 0, size, size);
    XSync(dpy, False);
}

static void
save_dump(const char *name, XImage *img)
{
    char path[1024];
    char safe[128];
    FILE *f;
    uint32_t hdr[3];
    size_t i;

    if (!opt_dumpdir)
        return;
    snprintf(safe, sizeof(safe), "%s", name);
    for (i = 0; safe[i]; i++)
        if (safe[i] == '/')
            safe[i] = '_';
    snprintf(path, sizeof(path), "%s/%s.raw", opt_dumpdir, safe);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    hdr[0] = img->width;
    hdr[1] = img->height;
    hdr[2] = img->depth;
    fwrite(hdr, sizeof(hdr), 1, f);
    fwrite(img->data, img->bytes_per_line, img->height, f);
    fclose(f);
}

static void
run_case(Display *dpy, FILE *out, const char *name,
         int op, Picture src, Picture mask, Picture dst,
         Pixmap dst_pm, int size)
{
    unsigned long long t0, t1;
    int b, k;
    int total = opt_batches * opt_per_batch;
    XImage *img;
    uint64_t sum;

    xerr_code = 0;
    for (k = 0; k < opt_warmup; k++)
        XRenderComposite(dpy, op, src, mask, dst,
                         0, 0, 0, 0, 0, 0, size, size);
    XSync(dpy, False);
    t0 = now_us();
    for (b = 0; b < opt_batches; b++) {
        for (k = 0; k < opt_per_batch; k++)
            XRenderComposite(dpy, op, src, mask, dst,
                             0, 0, 0, 0, 0, 0, size, size);
        XSync(dpy, False);
    }
    t1 = now_us();
    img = XGetImage(dpy, dst_pm, 0, 0, size, size, AllPlanes, ZPixmap);
    sum = img ? checksum_image(img) : 0;
    if (img) {
        save_dump(name, img);
        XDestroyImage(img);
    }
    fprintf(out, "%s,%d,%llu,%.2f,%016llx%s\n",
            name, total, t1 - t0,
            total ? (double) (t1 - t0) / total : 0.0,
            (unsigned long long) sum,
            xerr_code ? ",XERROR" : "");
    fflush(out);
}

/*
 * Single composite on a fresh dst. Timed rows above accumulate
 * N passes (converging toward src), which hides single-pass rounding.
 */
static void
run_once(Display *dpy, FILE *out, const char *name,
         int op, Picture src, Picture mask, Picture dst,
         Pixmap dst_pm, int size)
{
    unsigned long long t0, t1;
    XImage *img;
    uint64_t sum;

    xerr_code = 0;
    t0 = now_us();
    XRenderComposite(dpy, op, src, mask, dst,
                     0, 0, 0, 0, 0, 0, size, size);
    XSync(dpy, False);
    t1 = now_us();
    img = XGetImage(dpy, dst_pm, 0, 0, size, size, AllPlanes, ZPixmap);
    sum = img ? checksum_image(img) : 0;
    if (img) {
        save_dump(name, img);
        XDestroyImage(img);
    }
    fprintf(out, "%s,1,%llu,%.2f,%016llx%s\n",
            name, t1 - t0, (double) (t1 - t0),
            (unsigned long long) sum,
            xerr_code ? ",XERROR" : "");
    fflush(out);
}

int
main(int argc, char **argv)
{
    Display *dpy;
    const char *display = NULL;
    FILE *out = stdout;
    int render_event, render_error;
    const XRenderPictFormat *argbfmt, *a8fmt, *x2r10fmt;
    int scr;
    Drawable root;
    struct {
        const char *sname;
        const XRenderPictFormat *fmt;
        int depth;
        unsigned long l, r, dark;
    } groups[2];
    int ngroups = 0;
    struct {
        const char *oname;
        int op;
    } ops[] = {
        { "over", PictOpOver },
        { "src", PictOpSrc },
        { "add", PictOpAdd },
    };
    int sizes[] = { 64, 256, 1024 };
    int i, o, s, m;
    int arg;

    for (arg = 1; arg < argc; arg++) {
        if (!strcmp(argv[arg], "-display") && arg + 1 < argc)
            display = argv[++arg];
        else if (!strcmp(argv[arg], "-o") && arg + 1 < argc) {
            out = fopen(argv[++arg], "w");
            if (!out) {
                perror("fopen");
                return 1;
            }
        }
        else if (!strcmp(argv[arg], "-b") && arg + 1 < argc)
            opt_batches = atoi(argv[++arg]);
        else if (!strcmp(argv[arg], "-k") && arg + 1 < argc)
            opt_per_batch = atoi(argv[++arg]);
        else if (!strcmp(argv[arg], "-w") && arg + 1 < argc)
            opt_warmup = atoi(argv[++arg]);
        else if (!strcmp(argv[arg], "-m") && arg + 1 < argc)
            opt_maxsize = atoi(argv[++arg]);
        else if (!strcmp(argv[arg], "-s") && arg + 1 < argc)
            opt_dumpdir = argv[++arg];
        else {
            fprintf(stderr,
                    "usage: %s [-display d] [-o csv] [-b batches] "
                    "[-k per-batch] [-w warmup] [-m maxsize] "
                    "[-s dumpdir]\n", argv[0]);
            return 1;
        }
    }
    dpy = XOpenDisplay(display);
    if (!dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }
    XSetErrorHandler(xerr_handler);
    if (!XRenderQueryExtension(dpy, &render_event, &render_error)) {
        fprintf(stderr, "no RENDER extension\n");
        return 1;
    }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    argbfmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
    a8fmt = XRenderFindStandardFormat(dpy, PictStandardA8);
    if (!argbfmt || !a8fmt) {
        fprintf(stderr, "missing ARGB32/A8 formats\n");
        return 1;
    }
    x2r10fmt = find_x2r10(dpy);
    groups[ngroups].sname = "argb32";
    groups[ngroups].fmt = argbfmt;
    groups[ngroups].depth = 32;
    groups[ngroups].l = 0xffff0000;
    groups[ngroups].r = 0xff00ff00;
    groups[ngroups].dark = 0xff101010;
    ngroups++;
    if (x2r10fmt) {
        groups[ngroups].sname = "x2r10";
        groups[ngroups].fmt = x2r10fmt;
        groups[ngroups].depth = 30;
        groups[ngroups].l = 0x3ff00000;
        groups[ngroups].r = 0x000ffc00;
        groups[ngroups].dark = 0x00100401;
        ngroups++;
    }
    else {
        fprintf(stderr, "# note: no x2r10g10b10 format, skipping\n");
    }
    fprintf(out, "# alpha-map-bench batches=%d per_batch=%d warmup=%d\n",
            opt_batches, opt_per_batch, opt_warmup);
    fprintf(out, "case,comps,total_us,us_per_op,checksum,note\n");
    for (i = 0; i < ngroups; i++) {
        for (s = 0; s < 3; s++) {
            struct case_resources r;
            int size = sizes[s];
            char name[128];

            if (opt_maxsize && size > opt_maxsize)
                continue;
            if (!setup_group(dpy, root, groups[i].fmt,
                             groups[i].depth, a8fmt, argbfmt, size,
                             groups[i].l, groups[i].r, 0xc0, 0x40, &r)) {
                fprintf(stderr, "# note: skipping %s/%d (setup failed)\n",
                        groups[i].sname, size);
                continue;
            }
            for (o = 0; o < 3; o++) {
                for (m = 0; m < 2; m++) {
                    XRenderPictureAttributes cpa;

                    /* aligned fast path: origin zero */
                    memset(&cpa, 0, sizeof(cpa));
                    cpa.alpha_map = r.map;
                    cpa.alpha_x_origin = 0;
                    cpa.alpha_y_origin = 0;
                    XRenderChangePicture(dpy, r.src,
                                         CPAlphaMap | CPAlphaXOrigin |
                                         CPAlphaYOrigin, &cpa);
                    XSync(dpy, False);
                    snprintf(name, sizeof(name), "%s/%s/%s/%d/ox0",
                             groups[i].sname, ops[o].oname,
                             m ? "repmask" : "none", size);
                    reset_dst(dpy, &r, size, groups[i].dark);
                    run_case(dpy, out, name, ops[o].op, r.src,
                             m ? r.msk : None, r.dst, r.dst_pm, size);
                    /* fallback parity: nonzero origin */
                    if (!m && o < 2 && size >= 256) {
                        memset(&cpa, 0, sizeof(cpa));
                        cpa.alpha_map = r.map;
                        cpa.alpha_x_origin = 37;
                        cpa.alpha_y_origin = 11;
                        XRenderChangePicture(dpy, r.src,
                                             CPAlphaMap | CPAlphaXOrigin |
                                             CPAlphaYOrigin, &cpa);
                        XSync(dpy, False);
                        snprintf(name, sizeof(name), "%s/%s/none/%d/ox37y11",
                                 groups[i].sname, ops[o].oname, size);
                        reset_dst(dpy, &r, size, groups[i].dark);
                        run_case(dpy, out, name, ops[o].op, r.src,
                                 None, r.dst, r.dst_pm, size);
                    }
                    /* fallback parity: source transform; runs last for
                     * this group so no transform reset is needed
                     * (XRenderSetPictureTransform cannot take NULL) */
                    if (!m && o == 0 && size == 256) {
                        XTransform xf;

                        memset(&xf, 0, sizeof(xf));
                        xf.matrix[0][0] = XDoubleToFixed(1);
                        xf.matrix[1][1] = XDoubleToFixed(1);
                        xf.matrix[2][2] = XDoubleToFixed(1);
                        xf.matrix[0][2] = XDoubleToFixed(5);
                        xf.matrix[1][2] = XDoubleToFixed(3);
                        memset(&cpa, 0, sizeof(cpa));
                        cpa.alpha_map = r.map;
                        cpa.alpha_x_origin = 0;
                        cpa.alpha_y_origin = 0;
                        XRenderChangePicture(dpy, r.src,
                                             CPAlphaMap | CPAlphaXOrigin |
                                             CPAlphaYOrigin, &cpa);
                        XRenderSetPictureTransform(dpy, r.src, &xf);
                        XSync(dpy, False);
                        snprintf(name, sizeof(name), "%s/over/none/256/xform",
                                 groups[i].sname);
                        reset_dst(dpy, &r, size, groups[i].dark);
                        run_case(dpy, out, name, ops[o].op, r.src,
                                 None, r.dst, r.dst_pm, size);
                        /* reset by rebuilding the group below */
                        free_case(dpy, &r);
                        if (!setup_group(dpy, root, groups[i].fmt,
                                         groups[i].depth, a8fmt, argbfmt,
                                         size, groups[i].l, groups[i].r,
                                         0xc0, 0x40, &r))
                            goto next_size;
                    }
                }
            }
            /* one composite on a fresh dst each */
            if (size == 64 || size == 256) {
                XRenderPictureAttributes vpa;
                int vo;

                memset(&vpa, 0, sizeof(vpa));
                vpa.alpha_map = r.map;
                vpa.alpha_x_origin = 0;
                vpa.alpha_y_origin = 0;
                XRenderChangePicture(dpy, r.src,
                                     CPAlphaMap | CPAlphaXOrigin |
                                     CPAlphaYOrigin, &vpa);
                XSync(dpy, False);
                for (vo = 0; vo < 3; vo++) {
                    snprintf(name, sizeof(name), "%s/%s/none/%d/ox0/once",
                             groups[i].sname, ops[vo].oname, size);
                    reset_dst(dpy, &r, size, groups[i].dark);
                    run_once(dpy, out, name, ops[vo].op, r.src,
                             None, r.dst, r.dst_pm, size);
                }
                memset(&vpa, 0, sizeof(vpa));
                vpa.alpha_map = r.map;
                vpa.alpha_x_origin = 37;
                vpa.alpha_y_origin = 11;
                XRenderChangePicture(dpy, r.src,
                                     CPAlphaMap | CPAlphaXOrigin |
                                     CPAlphaYOrigin, &vpa);
                XSync(dpy, False);
                for (vo = 0; vo < 2; vo++) {
                    snprintf(name, sizeof(name),
                             "%s/%s/none/%d/ox37y11/once",
                             groups[i].sname, ops[vo].oname, size);
                    reset_dst(dpy, &r, size, groups[i].dark);
                    run_once(dpy, out, name, ops[vo].op, r.src,
                             None, r.dst, r.dst_pm, size);
                }
                if (size == 256) {
                    XTransform xf;

                    memset(&xf, 0, sizeof(xf));
                    xf.matrix[0][0] = XDoubleToFixed(1);
                    xf.matrix[1][1] = XDoubleToFixed(1);
                    xf.matrix[2][2] = XDoubleToFixed(1);
                    xf.matrix[0][2] = XDoubleToFixed(5);
                    xf.matrix[1][2] = XDoubleToFixed(3);
                    memset(&vpa, 0, sizeof(vpa));
                    vpa.alpha_map = r.map;
                    vpa.alpha_x_origin = 0;
                    vpa.alpha_y_origin = 0;
                    XRenderChangePicture(dpy, r.src,
                                         CPAlphaMap | CPAlphaXOrigin |
                                         CPAlphaYOrigin, &vpa);
                    XRenderSetPictureTransform(dpy, r.src, &xf);
                    XSync(dpy, False);
                    snprintf(name, sizeof(name),
                             "%s/over/none/256/xform/once",
                             groups[i].sname);
                    reset_dst(dpy, &r, size, groups[i].dark);
                    run_once(dpy, out, name, PictOpOver, r.src,
                             None, r.dst, r.dst_pm, size);
                }
            }
        next_size:
            free_case(dpy, &r);
        }
    }
    if (out != stdout)
        fclose(out);
    XCloseDisplay(dpy);
    return 0;
}
