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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
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
 */

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include "soft2d_priv.h"

DevPrivateKeyRec soft2d_pixmap_private_key;
DevPrivateKeyRec soft2d_screen_private_key;

void
soft2d_wait_on_pixmap(PixmapPtr pixmap)
{
    struct soft2d_pixmap_priv *priv;
    struct gbm_bo *bo = NULL;
    int fd;
    struct pollfd pfd;
    int ret;

    if (!pixmap)
        return;

    priv = soft2dGetPixmapPriv(pixmap);
    if (priv)
        bo = priv->bo;

    if (!bo)
        return;

    fd = gbm_bo_get_fd(bo);
    if (fd < 0)
        return;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    do {
        ret = xserver_poll(&pfd, 1, 1000);
    } while (ret < 0 && errno == EINTR);

    close(fd);
}

static inline void
soft2d_wait_on_drawable(DrawablePtr drawable)
{
    if (drawable->type == DRAWABLE_PIXMAP)
        soft2d_wait_on_pixmap((PixmapPtr)drawable);
    else if (drawable->type == DRAWABLE_WINDOW)
        soft2d_wait_on_pixmap(drawable->pScreen->GetWindowPixmap((WindowPtr)drawable));
}

static RegionPtr soft2d_copy_area(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                                  int srcx, int srcy, int width, int height,
                                  int dstx, int dsty);
static void soft2d_validate_gc(GCPtr pGC, unsigned long changes,
                               DrawablePtr drawable);
static int soft2d_create_gc(GCPtr pGC);
static void soft2d_destroy_gc(GCPtr pGC);

static GCOps soft2dGCOps;
static GCFuncs soft2dGCFuncs = {
    soft2d_validate_gc,
    miChangeGC,
    miCopyGC,
    soft2d_destroy_gc,
    miChangeClip,
    miDestroyClip,
    miCopyClip
};

static RegionPtr
soft2d_copy_area(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                 int srcx, int srcy, int width, int height,
                 int dstx, int dsty)
{
    soft2d_wait_on_drawable(pSrc);

    return fbCopyArea(pSrc, pDst, pGC, srcx, srcy, width, height, dstx, dsty);
}

static void
soft2d_get_image(DrawablePtr pDrawable, int x, int y, int w, int h,
                 unsigned int format, unsigned long planeMask, char *d)
{
    ScreenPtr screen = pDrawable->pScreen;
    struct soft2d_screen_priv *spriv = soft2dGetScreenPriv(screen);

    soft2d_wait_on_drawable(pDrawable);

    spriv->saved_GetImage(pDrawable, x, y, w, h, format, planeMask, d);
}

static void
soft2d_composite(CARD8 op, PicturePtr pSrc, PicturePtr pMask, PicturePtr pDst,
                 INT16 xSrc, INT16 ySrc, INT16 xMask, INT16 yMask,
                 INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr screen = pDst->pDrawable->pScreen;
    struct soft2d_screen_priv *spriv = soft2dGetScreenPriv(screen);

    if (pSrc && pSrc->pDrawable)
        soft2d_wait_on_drawable(pSrc->pDrawable);

    if (pMask && pMask->pDrawable)
        soft2d_wait_on_drawable(pMask->pDrawable);

    spriv->saved_Composite(op, pSrc, pMask, pDst, xSrc, ySrc,
                           xMask, yMask, xDst, yDst, width, height);

}

static void
soft2d_validate_gc(GCPtr pGC, unsigned long changes, DrawablePtr drawable)
{
    fbValidateGC(pGC, changes, drawable);
    pGC->ops = &soft2dGCOps;
}

static int
soft2d_create_gc(GCPtr pGC)
{
    ScreenPtr screen = pGC->pScreen;
    struct soft2d_screen_priv *spriv = soft2dGetScreenPriv(screen);
    int ret;

    ret = spriv->saved_CreateGC(pGC);
    if (ret) {
        pGC->funcs = &soft2dGCFuncs;
        pGC->ops = &soft2dGCOps;
    }

    return ret;
}

static void
soft2d_destroy_gc(GCPtr pGC)
{
    miDestroyGC(pGC);
}

static Bool
soft2d_gc_close_screen(ScreenPtr screen)
{
    struct soft2d_screen_priv *spriv = soft2dGetScreenPriv(screen);
    PictureScreenPtr ps = GetPictureScreenIfSet(screen);

    ps->Composite = spriv->saved_Composite;
    screen->GetImage = spriv->saved_GetImage;
    screen->CreateGC = spriv->saved_CreateGC;
    screen->CloseScreen = spriv->saved_GCCloseScreen;

    return screen->CloseScreen(screen);
}

Bool
soft2d_init_gc(ScreenPtr screen)
{
    struct soft2d_screen_priv *spriv = soft2dGetScreenPriv(screen);
    PictureScreenPtr ps = GetPictureScreenIfSet(screen);

    if (!spriv)
        return FALSE;

    spriv->saved_CreateGC = screen->CreateGC;
    screen->CreateGC = soft2d_create_gc;

    soft2dGCOps = fbGCOps;
    soft2dGCOps.CopyArea = soft2d_copy_area;

    spriv->saved_GetImage = screen->GetImage;
    screen->GetImage = soft2d_get_image;

    spriv->saved_Composite = ps->Composite;
    ps->Composite = soft2d_composite;

    spriv->saved_GCCloseScreen = screen->CloseScreen;
    screen->CloseScreen = soft2d_gc_close_screen;

    return TRUE;
}
