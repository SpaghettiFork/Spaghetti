/*
 * Copyright © 2010 Intel Corporation.
 * Copyright @ 2022 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *    Christopher Michael <cmichael@igalia.com>
 *    Juan A. Suarez <jasuarez@igalia.com>
 */

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include "xf86.h"
#include "driver.h"
#include "dri3.h"
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#ifdef FISSION_SOFT2D
static Bool soft2d_modifiers_get(ScreenPtr screen, uint32_t format,
                                  uint32_t *num, uint64_t **modifiers);

static void *
soft2d_pixmap_map_bo(msPixmapPrivPtr ppriv, struct gbm_bo *bo)
{
    void *baddr;
    size_t size;
    int fd;

    fd = gbm_bo_get_fd(bo);
    if (fd < 0)
        goto fail;

    size = lseek(fd, 0, SEEK_END);
    if (size == (size_t) -1) {
        close(fd);
        goto fail;
    }

    baddr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (baddr == MAP_FAILED)
        goto fail;

    ppriv->bo_map = baddr;
    ppriv->bo_map_size = size;
    return baddr;

fail:
    xf86DrvMsg(-1, X_ERROR, "Failed to map bo (%s)\n", strerror(errno));
    ppriv->bo = NULL;
    gbm_bo_destroy(bo);
    return NULL;
}

static Bool
soft2d_pixmap_make_exportable(PixmapPtr pixmap, Bool mods)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr pixmap_priv, exported_priv;
    uint32_t format = GBM_FORMAT_ARGB8888;
    struct gbm_bo *bo = NULL;
    PixmapPtr exported;
    void *baddr;
    GCPtr sgc;

    pixmap_priv = msGetPixmapPriv(&ms->drmmode, pixmap);

    if (pixmap_priv->bo &&
        (mods || !pixmap_priv->use_modifiers))
        return TRUE;

    if (pixmap->drawable.bitsPerPixel != 32)
        return FALSE;

    switch (pixmap->drawable.depth) {
    case 16:
        format = GBM_FORMAT_RGB565;
        break;
    case 24:
        format = GBM_FORMAT_XRGB8888;
        break;
    case 30:
        format = GBM_FORMAT_ARGB2101010;
        break;
    default:
        format = GBM_FORMAT_ARGB8888;
        break;
    }

    exported = fbCreatePixmap(screen, 0, 0, pixmap->drawable.depth, 0);
    exported_priv = msGetPixmapPriv(&ms->drmmode, exported);

    if (mods) {
        uint32_t num;
        uint64_t *modifiers = NULL;

        soft2d_modifiers_get(screen, format, &num, &modifiers);

        bo = gbm_bo_create_with_modifiers(ms->drmmode.gbm,
                                          pixmap->drawable.width,
                                          pixmap->drawable.height,
                                          format, modifiers, num);
        if (bo)
            exported_priv->use_modifiers = TRUE;
        free(modifiers);
    }

    if (!bo) {
        uint32_t flags = GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT;

        if (pixmap->usage_hint == CREATE_PIXMAP_USAGE_SHARED)
            flags |= GBM_BO_USE_LINEAR;

        bo = gbm_bo_create(ms->drmmode.gbm, pixmap->drawable.width,
                           pixmap->drawable.height, format, flags);
        if (!bo)
            goto map_fail;
    }

    exported_priv->bo = bo;

    baddr = soft2d_pixmap_map_bo(exported_priv, bo);
    if (!baddr)
        goto map_fail;

    screen->ModifyPixmapHeader(exported, pixmap->drawable.width,
                               pixmap->drawable.height, 0, 0,
                               gbm_bo_get_stride(bo), baddr);

    sgc = GetScratchGC(pixmap->drawable.depth, screen);
    ValidateGC(&pixmap->drawable, sgc);
    sgc->ops->CopyArea(&pixmap->drawable, &exported->drawable, sgc, 0, 0,
                       pixmap->drawable.width, pixmap->drawable.height, 0, 0);
    FreeScratchGC(sgc);

    /* swap gbm_bo, data, etc */
    soft2d_buffers_exchange(pixmap, exported);

    screen->ModifyPixmapHeader(pixmap, pixmap->drawable.width,
                               pixmap->drawable.height, 0, 0,
                               exported->devKind, baddr);

    fbDestroyPixmap(exported);
    return TRUE;

map_fail:
    fbDestroyPixmap(exported);
    return FALSE;
}

Bool
soft2d_pixmap_from_gbm_bo(PixmapPtr pixmap, struct gbm_bo *bo)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    uint32_t stride, w, h;

    w = gbm_bo_get_width(bo);
    h = gbm_bo_get_height(bo);
    stride = gbm_bo_get_stride(bo);

    screen->ModifyPixmapHeader(pixmap, w, h, 0, 0, stride, NULL);
    return TRUE;
}

Bool
soft2d_back_pixmap_from_fd(PixmapPtr pixmap, int fd,
                            CARD16 width, CARD16 height,
                            CARD16 stride, CARD8 depth, CARD8 bpp)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    struct gbm_import_fd_data import = { 0 };
    struct gbm_bo *bo;
    void *baddr;

    if ((bpp != 32) ||
        !(depth == 24 || depth == 32 || depth == 30) || width == 0 || height == 0)
        return FALSE;

    import.fd = fd;
    import.width = width;
    import.height = height;
    import.stride = stride;

    switch (depth) {
    case 16:
        import.format = GBM_FORMAT_RGB565;
        break;
    case 24:
        import.format = GBM_FORMAT_XRGB8888;
        break;
    case 30:
        import.format = GBM_FORMAT_ARGB2101010;
        break;
    default:
        import.format = GBM_FORMAT_ARGB8888;
        break;
    }

    bo = gbm_bo_import(ms->drmmode.gbm, GBM_BO_IMPORT_FD, &import, 0);
    if (!bo) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to import bo (%s)\n", strerror(errno));
        return FALSE;
    }

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
    ppriv->bo = bo;
    ppriv->use_modifiers = FALSE;

    baddr = soft2d_pixmap_map_bo(ppriv, bo);
    if (!baddr) {
        return FALSE;
    }

    return screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, baddr);
}

static int
soft2d_render_node(int fd, struct stat *st)
{
    if (fstat(fd, st)) return -1;
    if (!S_ISCHR(st->st_mode)) return -1;
    return (st->st_rdev & 0x80);
}

static int
soft2d_open(ScreenPtr screen, RRProviderPtr provider, int *out)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    struct stat buff;
    char *dev;
    int fd = -1;

    dev = drmGetDeviceNameFromFd2(ms->fd);

#ifdef O_CLOEXEC
    fd = open(dev, O_RDWR | O_CLOEXEC);
#endif
    if (fd < 0)
        fd = open(dev, O_RDWR);

    free(dev);
    if (fd < 0)
        return -BadMatch;

    if (fstat(fd, &buff)) {
        close(fd);
        return -BadMatch;
    }

    if (!soft2d_render_node(fd, &buff)) {
        drm_magic_t magic;

        if ((drmGetMagic(fd, &magic)) || (drmAuthMagic(ms->fd, magic))) {
            close(fd);
            return -BadMatch;
        }
    }

    *out = fd;
    return Success;
}

static inline Bool
soft2d_find_modifier(uint64_t modifier, const uint64_t *modifiers, unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count; i++) {
        if (modifiers[i] == modifier)
           return TRUE;
    }

    return FALSE;
}

static inline uint32_t
soft2d_alpha_twin_format_get(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_XRGB8888:
        return DRM_FORMAT_ARGB8888;
    case DRM_FORMAT_ARGB8888:
        return DRM_FORMAT_XRGB8888;
    case DRM_FORMAT_XBGR8888:
        return DRM_FORMAT_ABGR8888;
    case DRM_FORMAT_ABGR8888:
        return DRM_FORMAT_XBGR8888;
    case DRM_FORMAT_XRGB2101010:
        return DRM_FORMAT_ARGB2101010;
    case DRM_FORMAT_ARGB2101010:
        return DRM_FORMAT_XRGB2101010;
    case DRM_FORMAT_XBGR2101010:
        return DRM_FORMAT_ABGR2101010;
    case DRM_FORMAT_ABGR2101010:
        return DRM_FORMAT_XBGR2101010;
    default:
        return 0;
    }
}

static Bool
soft2d_formats_get(ScreenPtr screen, CARD32 *num, CARD32 **formats)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    xf86CrtcConfigPtr xcfg = XF86_CRTC_CONFIG_PTR(scrn);
    int c = 0, i = 0, j = 0, n = 0;

    *num = 0;

    for (; c < xcfg->num_crtc; c++) {
        xf86CrtcPtr crtc = xcfg->crtc[c];
        drmmode_crtc_private_ptr dcrtc = crtc->driver_private;

        if (!crtc->enabled)
            continue;

        if (dcrtc->num_formats == 0)
            continue;

        *formats = calloc(dcrtc->num_formats * 2, sizeof(CARD32));
        if (!*formats)
            return FALSE;

        for (i = 0; i < dcrtc->num_formats; i++) {
            uint32_t f = dcrtc->formats[i].format;

            for (j = 0; j < n; j++) {
                if ((*formats)[j] == f)
                    break;
            }
            if (j == n)
                (*formats)[n++] = f;

            f = soft2d_alpha_twin_format_get(f);
            if (f) {
                for (j = 0; j < n; j++) {
                    if ((*formats)[j] == f)
                        break;
                }
                if (j == n)
                    (*formats)[n++] = f;
            }
        }

        *num = n;
        break;
    }

    return TRUE;
}

static inline uint32_t
soft2d_opaque_format_get(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_ARGB8888:
        return DRM_FORMAT_XRGB8888;
    case DRM_FORMAT_ARGB2101010:
        return DRM_FORMAT_XRGB2101010;
    default:
        return format;
    }
}

static Bool
soft2d_modifiers_get(ScreenPtr screen, uint32_t format,
                      uint32_t *num, uint64_t **modifiers)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);

    *num = 0;
    *modifiers = NULL;

    xf86CrtcConfigPtr xcfg = XF86_CRTC_CONFIG_PTR(scrn);
    drmmode_format_ptr dformat = NULL;
    int c = 0, i = 0;

    format = soft2d_opaque_format_get(format);

    for (; c < xcfg->num_crtc; c++) {
        xf86CrtcPtr crtc = xcfg->crtc[c];
        drmmode_crtc_private_ptr dcrtc = crtc->driver_private;

        if (!crtc->enabled)
            continue;

        if (dcrtc->num_formats == 0)
            continue;

        for (i = 0; i < dcrtc->num_formats; i++) {
            if (dcrtc->formats[i].format == format) {
                dformat = &dcrtc->formats[i];

                free(*modifiers);
                *modifiers = calloc(dformat->num_modifiers, sizeof(uint64_t));
                if (!*modifiers)
                    return FALSE;
                memcpy(*modifiers, dformat->modifiers, dformat->num_modifiers * sizeof(uint64_t));
                *num = dformat->num_modifiers;
                break;
            }
        }
    }

    return TRUE;
}

static Bool
soft2d_get_drawable_modifiers(DrawablePtr draw, uint32_t format,
                               uint32_t *num, uint64_t **modifiers)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(draw->pScreen);

    *num = 0;
    *modifiers = NULL;

    xf86CrtcConfigPtr xcfg = XF86_CRTC_CONFIG_PTR(scrn);
    drmmode_format_ptr dformat = NULL;
    int c = 0, i = 0, j = 0;

    format = soft2d_opaque_format_get(format);

    for (; c < xcfg->num_crtc; c++) {
        xf86CrtcPtr crtc = xcfg->crtc[c];
        drmmode_crtc_private_ptr dcrtc = crtc->driver_private;

        if (!crtc->enabled)
            continue;

        if (dcrtc->num_formats == 0)
            continue;

        for (i = 0; i < dcrtc->num_formats; i++) {
            if ((dcrtc->formats[i].format == format) &&
                (soft2d_find_modifier(DRM_FORMAT_MOD_LINEAR,
                                       dcrtc->formats[i].modifiers,
                                       dcrtc->formats[i].num_modifiers))) {
                dformat = &dcrtc->formats[i];
                for (j = 0; j < dformat->num_modifiers; j++) {
                    if (dformat->modifiers[j] == DRM_FORMAT_MOD_LINEAR) {
                        free(*modifiers);
                        *modifiers = calloc(1, sizeof(uint64_t));
                        if (!*modifiers)
                            return FALSE;
                        **modifiers = dformat->modifiers[j];
                        *num = 1;
                        return TRUE;
                    }
                }
            }
        }
    }

    return TRUE;
}

PixmapPtr
soft2d_pixmap_from_fds(ScreenPtr screen, CARD8 num, const int *fds,
                        CARD16 width, CARD16 height, const CARD32 *strides,
                        const CARD32 *offsets, CARD8 depth, CARD8 bpp, uint64_t modifier)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    PixmapPtr pixmap;
    Bool ret = FALSE;
    int i;

    pixmap = fbCreatePixmap(screen, 0, 0, depth, 0);

    if (modifier != DRM_FORMAT_MOD_INVALID) {
        struct gbm_import_fd_modifier_data import = {0};
        struct gbm_bo *bo;

        import.width = width;
        import.height = height;
        import.num_fds = num;
        import.modifier = modifier;
        for (i = 0; i < num; i++) {
            import.fds[i] = fds[i];
            import.strides[i] = strides[i];
            import.offsets[i] = offsets[i];
        }

        switch (depth) {
        case 16:
            import.format = GBM_FORMAT_RGB565;
            break;
        case 24:
            import.format = GBM_FORMAT_XRGB8888;
            break;
        case 30:
            import.format = GBM_FORMAT_ARGB2101010;
            break;
        default:
            import.format = GBM_FORMAT_ARGB8888;
            break;
        }

        bo = gbm_bo_import(ms->drmmode.gbm, GBM_BO_IMPORT_FD_MODIFIER, &import, 0);
        if (bo) {
            msPixmapPrivPtr ppriv;
            void *baddr;

            ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
            ppriv->bo = bo;
            ppriv->use_modifiers = TRUE;

            baddr = soft2d_pixmap_map_bo(ppriv, bo);
            if (!baddr)
                goto map_fail;

            screen->ModifyPixmapHeader(pixmap, width, height, 0, 0,
                                       strides[0], baddr);
            ret = TRUE;
        }
    } else {
        if (num == 1)
            ret = soft2d_back_pixmap_from_fd(pixmap, fds[0], width, height,
                                              strides[0], depth, bpp);
    }

    if (!screen->SetSharedPixmapBacking(pixmap, (void *)(intptr_t)fds[0]))
        ret = FALSE;

map_fail:
    if (ret == FALSE) {
        fbDestroyPixmap(pixmap);
        return NULL;
    }

    return pixmap;
}

static int
soft2d_fds_from_pixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds,
                        uint32_t *strides, uint32_t *offsets, uint64_t *modifier)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    struct gbm_bo *bo;
    int num = 0, i;

    if (!soft2d_pixmap_make_exportable(pixmap, TRUE))
        return 0;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);

    if (!ppriv->bo)
        ppriv->bo = soft2d_gbm_bo_from_pixmap(screen, pixmap);

    bo = ppriv->bo;
    if (!bo) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "soft2d_fds_from_pixmap: pixmap has no bo\n");
        return 0;
    }

    num = gbm_bo_get_plane_count(bo);
    for (i = 0; i < num; i++) {
        fds[i] = gbm_bo_get_fd(bo);
        strides[i] = gbm_bo_get_stride_for_plane(bo, i);
        offsets[i] = gbm_bo_get_offset(bo, i);
    }
    *modifier = gbm_bo_get_modifier(bo);
    return num;
}

int
soft2d_shareable_fd_from_pixmap(ScreenPtr screen, PixmapPtr pixmap,
                                 CARD16 *stride, CARD32 *size)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    unsigned ohint = pixmap->usage_hint;
    msPixmapPrivPtr ppriv;
    struct gbm_bo *bo;
    int fd = -1;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
    if (!ppriv)
        return -1;

    pixmap->usage_hint = CREATE_PIXMAP_USAGE_SHARED;

    if (!ppriv->bo)
        ppriv->bo = soft2d_gbm_bo_from_pixmap(screen, pixmap);

    bo = ppriv->bo;
    if (!bo)
        goto out;

    fd = gbm_bo_get_fd(bo);
    *stride = gbm_bo_get_stride(bo);
    *size = *stride * gbm_bo_get_height(bo);

out:
    pixmap->usage_hint = ohint;
    return fd;
}

struct gbm_bo *
soft2d_gbm_bo_from_pixmap(ScreenPtr screen, PixmapPtr pixmap)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    uint32_t format = GBM_FORMAT_ARGB8888;
    struct gbm_bo *bo;
    uint32_t num;
    uint64_t *modifiers = NULL;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
    if (pixmap == screen->GetScreenPixmap(screen) && (!ppriv || !ppriv->bo))
        return NULL;

    if (!soft2d_pixmap_make_exportable(pixmap, TRUE))
        return NULL;

    if (ppriv->bo)
        return ppriv->bo;

    switch (pixmap->drawable.depth) {
    case 16:
        format = GBM_FORMAT_RGB565;
        break;
    case 24:
        format = GBM_FORMAT_XRGB8888;
        break;
    case 30:
        format = GBM_FORMAT_ARGB2101010;
        break;
    default:
        format = GBM_FORMAT_ARGB8888;
        break;
    }

    soft2d_modifiers_get(screen, format, &num, &modifiers);

    bo = gbm_bo_create_with_modifiers(ms->drmmode.gbm,
                                      pixmap->drawable.width,
                                      pixmap->drawable.height,
                                      format, modifiers, num);
    free(modifiers);

    if (!bo) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to make GBM bo (%s)\n", strerror(errno));
        return NULL;
    }

    return bo;
}

Bool
soft2d_destroy_pixmap(PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    if (pixmap->refcnt == 1) {
        msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);

        if (ppriv && ppriv->bo) {
            if (ppriv->bo_map)
                munmap(ppriv->bo_map, ppriv->bo_map_size);
            gbm_bo_destroy(ppriv->bo);
        }
    }

    fbDestroyPixmap(pixmap);
    return TRUE;
}

static Bool
soft2d_flink_name_get(int fd, int handle, int *name)
{
    struct drm_gem_flink f;

    f.handle = handle;
    if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &f) < 0) {
        if (errno == ENODEV) {
            *name = handle;
            return TRUE;
        } else {
            return FALSE;
        }
    }

    *name = f.name;
    return TRUE;
}

static void
soft2d_bo_name_get(int fd, struct gbm_bo *bo, int *name)
{
    union gbm_bo_handle hdl;

    hdl = gbm_bo_get_handle(bo);
    if (!soft2d_flink_name_get(fd, hdl.u32, name))
      *name = -1;
}

int
soft2d_pixmap_name_get(PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    struct gbm_bo *bo;
    int fd = -1;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);

    if (!soft2d_pixmap_make_exportable(pixmap, TRUE))
        goto fail;

    bo = ppriv->bo;
    if (!bo)
        goto fail;

    pixmap->devKind = gbm_bo_get_stride(bo);
    soft2d_bo_name_get(ms->fd, bo, &fd);
    *stride = pixmap->devKind;
    *size = pixmap->devKind * gbm_bo_get_height(bo);

fail:
    return fd;
}

void
soft2d_buffers_exchange(PixmapPtr front, PixmapPtr back)
{
    ScreenPtr screen = front->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr fpriv, bpriv;

    fpriv = msGetPixmapPriv(&ms->drmmode, front);
    bpriv = msGetPixmapPriv(&ms->drmmode, back);

    XORG_EXCHANGE(fpriv->use_modifiers, bpriv->use_modifiers)
    XORG_EXCHANGE(fpriv->bo,            bpriv->bo)
    XORG_EXCHANGE(fpriv->bo_map,        bpriv->bo_map)
    XORG_EXCHANGE(fpriv->bo_map_size,   bpriv->bo_map_size)
}

static const dri3_screen_info_rec soft2d_screen_info = {
    .version = 2,

    .open = soft2d_open,
    .fd_from_pixmap = soft2d_shareable_fd_from_pixmap,

    .pixmap_from_fds = soft2d_pixmap_from_fds,
    .fds_from_pixmap = soft2d_fds_from_pixmap,

    .get_formats = soft2d_formats_get,
    .get_modifiers = soft2d_modifiers_get,
    .get_drawable_modifiers = soft2d_get_drawable_modifiers,
};

Bool
soft2d_screen_init(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    Bool ret = FALSE;

    ms->drmmode.destroy_pixmap = screen->DestroyPixmap;
    screen->DestroyPixmap = soft2d_destroy_pixmap;

    ret = dri3_screen_init(screen, &soft2d_screen_info);
    if (!ret)
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "soft2d: Failed to initialize DRI3\n");

    return ret;
}
#endif