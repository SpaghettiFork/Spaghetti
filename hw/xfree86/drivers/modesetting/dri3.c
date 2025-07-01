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

#ifdef MS_DRI3
static Bool ms_dri3_modifiers_get(ScreenPtr screen, uint32_t format, uint32_t *num, uint64_t **modifiers);

static void *
ms_dri3_pixmap_map_bo(msPixmapPrivPtr ppriv, struct gbm_bo *bo)
{
    void *baddr;
    uint32_t bstride, bw, bh;

    bw = gbm_bo_get_width(bo);
    bh = gbm_bo_get_height(bo);
    baddr = gbm_bo_map(bo, 0, 0, bw, bh, GBM_BO_TRANSFER_READ_WRITE,
                       &bstride, &ppriv->bo_map);

    if (baddr == MAP_FAILED) {
       xf86DrvMsg(-1, X_ERROR, "Failed to map bo: %s\n", strerror(errno));
       ppriv->bo = NULL;
       gbm_bo_destroy(bo);
       return NULL;
    }

   return baddr;
}

static Bool
ms_dri3_pixmap_make_exportable(PixmapPtr pixmap, Bool mods)
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

    if (pixmap->drawable.bitsPerPixel != 32) return FALSE;

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

       ms_dri3_modifiers_get(screen, format, &num, &modifiers);

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
       if (!bo) goto map_fail;
    }

    exported_priv->bo = bo;

    baddr = ms_dri3_pixmap_map_bo(exported_priv, bo);
    if (!baddr) goto map_fail;

    screen->ModifyPixmapHeader(exported, pixmap->drawable.width,
                               pixmap->drawable.height, 0, 0,
                               gbm_bo_get_stride(bo), baddr);

    sgc = GetScratchGC(pixmap->drawable.depth, screen);
    ValidateGC(&pixmap->drawable, sgc);
    sgc->ops->CopyArea(&pixmap->drawable, &exported->drawable, sgc, 0, 0,
                       pixmap->drawable.width, pixmap->drawable.height, 0, 0);
    FreeScratchGC(sgc);

    /* swap gbm_bo, data, etc */
    ms_dri3_buffers_exchange(pixmap, exported);

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
ms_dri3_pixmap_from_gbm_bo(PixmapPtr pixmap, struct gbm_bo *bo)
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
ms_dri3_back_pixmap_from_fd(PixmapPtr pixmap, int fd, CARD16 width, CARD16 height, CARD16 stride, CARD8 depth, CARD8 bpp)
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
    import.format = GBM_FORMAT_ARGB8888;

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
       xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to import bo: %s\n",
                  strerror(errno));
       return FALSE;
    }

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
    ppriv->bo = bo;
    ppriv->use_modifiers = FALSE;

    baddr = ms_dri3_pixmap_map_bo(ppriv, bo);
    if (!baddr) {
       ppriv->bo = NULL;
       gbm_bo_destroy(bo);
       xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to map bo: %s\n",
                  strerror(errno));
       return FALSE;
    }

    screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, baddr);

    return TRUE;
}

Bool
ms_dri3_create_back_pixmap(PixmapPtr pixmap, int handle, int stride)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    int ret, fd;

    ret = drmPrimeHandleToFD(ms->fd, handle, O_CLOEXEC | O_RDWR, &fd);
    if (ret) {
       xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                  "Failed to make prime FD for handle: %d\n", handle);
       return FALSE;
    }

    if (!ms_dri3_back_pixmap_from_fd(pixmap, fd, pixmap->drawable.width,
                                     pixmap->drawable.height, stride,
                                     pixmap->drawable.depth,
                                     pixmap->drawable.bitsPerPixel)) {
       xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                  "Failed to make import prime FD as pixmap: %d\n", errno);
       close(fd);
       return FALSE;
    }

    close(fd);
    return TRUE;
}

static int
ms_dri3_render_node(int fd, struct stat *st)
{
    if (fstat(fd, st)) return -1;
    if (!S_ISCHR(st->st_mode)) return -1;
    return (st->st_rdev & 0x80);
}

static int
ms_dri3_open(ScreenPtr screen, RRProviderPtr provider, int *out)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    struct stat buff;
    char *dev;
    int fd = -1;

    dev = drmGetDeviceNameFromFd2(ms->fd);

# ifdef O_CLOEXEC
    fd = open(dev, O_RDWR | O_CLOEXEC);
# endif
    if (fd < 0)
       fd = open(dev, O_RDWR);

    free(dev);
    if (fd < 0) return -BadMatch;

    if (fstat(fd, &buff)) {
       close(fd);
       return -BadMatch;
    }

    if (!ms_dri3_render_node(fd, &buff)) {
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
ms_dri3_find_modifier(uint64_t modifier, const uint64_t *modifiers, unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count; i++) {
        if (modifiers[i] == modifier)
           return TRUE;
    }

   return FALSE;
}

static Bool
ms_dri3_formats_get(ScreenPtr screen, CARD32 *num, CARD32 **formats)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    xf86CrtcConfigPtr xcfg = XF86_CRTC_CONFIG_PTR(scrn);
    modesettingPtr ms = modesettingPTR(scrn);
    int c = 0, i = 0;

    *num = 0;

    if (ms->atomic_modeset || ms->kms_has_modifiers) {
       for (; c < xcfg->num_crtc; c++) {
           xf86CrtcPtr crtc = xcfg->crtc[c];
           drmmode_crtc_private_ptr dcrtc = crtc->driver_private;

           if (!crtc->enabled)
              continue;

           if (dcrtc->num_formats == 0)
              continue;

           *formats = calloc(dcrtc->num_formats, sizeof(CARD32));
           if (!*formats)
              return FALSE;

           for (i = 0; i < dcrtc->num_formats; i++)
               (*formats)[i] = dcrtc->formats[i].format;

           *num = dcrtc->num_formats;
           break;
       }
    }

    return TRUE;
}

static inline uint32_t
ms_dri3_opaque_format_get(uint32_t format)
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
ms_dri3_modifiers_get(ScreenPtr screen, uint32_t format, uint32_t *num, uint64_t **modifiers)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    *num = 0;
    *modifiers = NULL;

    if (ms->atomic_modeset || ms->kms_has_modifiers) {
       xf86CrtcConfigPtr xcfg = XF86_CRTC_CONFIG_PTR(scrn);
       drmmode_format_ptr dformat = NULL;
       int c = 0, i = 0, j = 0;

       format = ms_dri3_opaque_format_get(format);

       for (; c < xcfg->num_crtc; c++) {
           xf86CrtcPtr crtc = xcfg->crtc[c];
           drmmode_crtc_private_ptr dcrtc = crtc->driver_private;

           if (!crtc->enabled)
              continue;

           if (dcrtc->num_formats == 0)
              continue;

           for (i = 0; i < dcrtc->num_formats; i++) {
               if ((dcrtc->formats[i].format == format) &&
                   (ms_dri3_find_modifier(DRM_FORMAT_MOD_LINEAR,
                                          dcrtc->formats[i].modifiers,
                                          dcrtc->formats[i].num_modifiers))) {
                  dformat = &dcrtc->formats[i];
                  for (j = 0; j < dformat->num_modifiers; j++) {
                      if (dformat->modifiers[j] == DRM_FORMAT_MOD_LINEAR) {
                         free(*modifiers);
                         *modifiers = calloc(1, sizeof(uint64_t));
                         if (!*modifiers) return FALSE;
                         **modifiers = dformat->modifiers[j];
                         *num = 1;
                         return TRUE;
                      }
                  }
               }
           }
       }
    }

    return TRUE;
}

static Bool
ms_dri3_drawable_modifiers_get(DrawablePtr draw, uint32_t format, uint32_t *num, uint64_t **modifiers)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(draw->pScreen);
    modesettingPtr ms = modesettingPTR(scrn);

    if (ms->drmmode.get_drawable_modifiers)
       return ms->drmmode.get_drawable_modifiers(draw, format, num, modifiers);

    *num = 0;
    *modifiers = NULL;
    return TRUE;
}

static PixmapPtr
ms_dri3_pixmap_from_fds(ScreenPtr screen, CARD8 num, const int *fds, CARD16 width, CARD16 height, const CARD32 *strides, const CARD32 *offsets, CARD8 depth, CARD8 bpp, uint64_t modifier)
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

          baddr = ms_dri3_pixmap_map_bo(ppriv, bo);
          if (!baddr) goto map_fail;

          screen->ModifyPixmapHeader(pixmap, width, height, 0, 0,
                                     strides[0], baddr);
          ret = TRUE;
       }
    } else {
       if (num == 1)
          ret = ms_dri3_back_pixmap_from_fd(pixmap, fds[0], width, height,
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
ms_dri3_fds_from_pixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds, uint32_t *strides, uint32_t *offsets, uint64_t *modifier)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    struct gbm_bo *bo;
    int num = 0, i;

    if (!ms_dri3_pixmap_make_exportable(pixmap, TRUE))
       return 0;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);

    if (!ppriv->bo)
       ppriv->bo = ms_dri3_gbm_bo_from_pixmap(screen, pixmap);

    bo = ppriv->bo;
    if (!bo) {
       xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                  "ms_dri3_fds_from_pixmap: pixmap has no bo\n");
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
ms_dri3_shareable_fd_from_pixmap(ScreenPtr screen, PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    unsigned ohint = pixmap->usage_hint;
    msPixmapPrivPtr ppriv;
    struct gbm_bo *bo;
    int fd = -1;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
    if (!ppriv) return -1;

    pixmap->usage_hint = CREATE_PIXMAP_USAGE_SHARED;

    if (!ppriv->bo)
     ppriv->bo = ms_dri3_gbm_bo_from_pixmap(screen, pixmap);

    bo = ppriv->bo;
    if (!bo) goto out;

    fd = gbm_bo_get_fd(bo);
    *stride = gbm_bo_get_stride(bo);
    *size = *stride * gbm_bo_get_height(bo);

out:
    pixmap->usage_hint = ohint;
    return fd;
}

struct gbm_bo *
ms_dri3_gbm_bo_from_pixmap(ScreenPtr screen, PixmapPtr pixmap)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    uint32_t format = GBM_FORMAT_ARGB8888;
    struct gbm_bo *bo;
    uint32_t num;
    uint64_t *modifiers = NULL;

    if (!ms_dri3_pixmap_make_exportable(pixmap, TRUE)) return NULL;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);
    if (ppriv->bo) return ppriv->bo;

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

    ms_dri3_modifiers_get(screen, format, &num, &modifiers);

    bo = gbm_bo_create_with_modifiers(ms->drmmode.gbm,
                                      pixmap->drawable.width,
                                      pixmap->drawable.height,
                                      format, modifiers, num);
    free(modifiers);

    if (!bo) {
       xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to make GBM bo: %s\n",
                  strerror(errno));
       return NULL;
    }

   return bo;
}

void
ms_dri3_set_drawable_modifiers_func(ScreenPtr screen, GetDrawableModifiersFuncPtr func)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);

    ms->drmmode.get_drawable_modifiers = func;
}

Bool
ms_dri3_destroy_pixmap(PixmapPtr pixmap)
{
   ScreenPtr screen = pixmap->drawable.pScreen;
   ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
   modesettingPtr ms = modesettingPTR(scrn);

   if (pixmap->refcnt == 1) {
      msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);

      if (ppriv && ppriv->bo) {
         gbm_bo_unmap(ppriv->bo, ppriv->bo_map);
         gbm_bo_destroy(ppriv->bo);
      }
   }

   fbDestroyPixmap(pixmap);

   return TRUE;
}

static Bool
ms_dri3_flink_name_get(int fd, int handle, int *name)
{
    struct drm_gem_flink f;

    f.handle = handle;
    if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &f) < 0) {
       if (errno == ENODEV) {
          *name = handle;
          return TRUE;
       }
       else
         return FALSE;
    }

    *name = f.name;
    return TRUE;
}

static void
ms_dri3_bo_name_get(int fd, struct gbm_bo *bo, int *name)
{
    union gbm_bo_handle hdl;

    hdl = gbm_bo_get_handle(bo);
    if (!ms_dri3_flink_name_get(fd, hdl.u32, name))
      *name = -1;
}

int
ms_dri3_pixmap_name_get(PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr ppriv;
    struct gbm_bo *bo;
    int fd = -1;

    ppriv = msGetPixmapPriv(&ms->drmmode, pixmap);

    if (!ms_dri3_pixmap_make_exportable(pixmap, TRUE))
      goto fail;

    bo = ppriv->bo;
    if (!bo) goto fail;

    pixmap->devKind = gbm_bo_get_stride(bo);
    ms_dri3_bo_name_get(ms->fd, bo, &fd);
    *stride = pixmap->devKind;
    *size = pixmap->devKind * gbm_bo_get_height(bo);

fail:
    return fd;
}

void
ms_dri3_buffers_exchange(PixmapPtr front, PixmapPtr back)
{
    ScreenPtr screen = front->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    msPixmapPrivPtr fpriv, bpriv;
    Bool tmp_mod = FALSE;
    struct gbm_bo *tmp_bo = NULL;
    void *tmp_map;

    fpriv = msGetPixmapPriv(&ms->drmmode, front);
    bpriv = msGetPixmapPriv(&ms->drmmode, back);

    tmp_bo = bpriv->bo;
    tmp_map = bpriv->bo_map;
    tmp_mod = bpriv->use_modifiers;

    bpriv->bo = fpriv->bo;
    bpriv->bo_map = fpriv->bo_map;
    bpriv->use_modifiers = fpriv->use_modifiers;

    fpriv->bo = tmp_bo;
    fpriv->bo_map = tmp_map;
    fpriv->use_modifiers = tmp_mod;
}

static dri3_screen_info_rec ms_dri3_screen_info =
{
    .version = DRI3_SCREEN_INFO_VERSION,
    .open = ms_dri3_open,
    .get_formats = ms_dri3_formats_get,
    .get_modifiers = ms_dri3_modifiers_get,
    .get_drawable_modifiers = ms_dri3_drawable_modifiers_get,
    .pixmap_from_fds = ms_dri3_pixmap_from_fds,
    .fds_from_pixmap = ms_dri3_fds_from_pixmap,
};

Bool
ms_dri3_screen_init(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    Bool ret = FALSE;

    ms->drmmode.destroy_pixmap = screen->DestroyPixmap;
    screen->DestroyPixmap = ms_dri3_destroy_pixmap;

    ret = dri3_screen_init(screen, &ms_dri3_screen_info);
    if (!ret)
       xf86DrvMsg(-1, X_ERROR, "dri3_screen_init Failed !\n");

    return ret;
}
#endif