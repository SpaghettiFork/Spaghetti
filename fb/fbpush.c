/*
 * Copyright © 1998 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "fb.h"

static void
fbPushFill(DrawablePtr pDrawable,
           GCPtr pGC,
           FbStip * src,
           FbStride srcStride, int srcX, int x, int y, int width, int height)
{
    FbGCPrivPtr pPriv = fbGetGCPrivate(pGC);
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    int dstX, dstWidth;

    fbGetDrawable(pDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);
    dst = dst + (y + dstYoff) * dstStride;
    dstX = (x + dstXoff) * dstBpp;
    dstWidth = width * dstBpp;

    if (pGC->fillStyle == FillSolid) {
        if (dstBpp == 1) {
            fbBltStip(src, srcStride, srcX,
                      (FbStip *) dst,
                      FbBitsStrideToStipStride(dstStride),
                      dstX, dstWidth, height,
                      FbStipple1Rop(pGC->alu, pGC->fgPixel), pPriv->pm, dstBpp);
        }
        else {
            fbBltOne(src, srcStride, srcX,
                     dst, dstStride, dstX, dstBpp, dstWidth, height,
                     pPriv->and, pPriv->xor,
                     fbAnd(GXnoop, (FbBits) 0, FB_ALLONES),
                     fbXor(GXnoop, (FbBits) 0, FB_ALLONES));
        }
    }
    else {
        FbBits bgand, bgxor;

        if (pGC->fillStyle == FillStippled) {
            bgand = FB_ALLONES;
            bgxor = 0;
        }
        else {
            bgand = pPriv->bgand;
            bgxor = pPriv->bgxor;
        }

        if (dstBpp == 1) {
            fbBltStip(src, srcStride, srcX,
                      (FbStip *) dst,
                      FbBitsStrideToStipStride(dstStride),
                      dstX, dstWidth, height,
                      (pGC->fillStyle == FillStippled)
                          ? FbStipple1Rop(pGC->alu, pGC->fgPixel)
                          : FbOpaqueStipple1Rop(pGC->alu, pGC->fgPixel,
                                                pGC->bgPixel),
                      pPriv->pm, dstBpp);
        }
        else {
            fbBltOne(src, srcStride, srcX,
                     dst, dstStride, dstX, dstBpp, dstWidth, height,
                     pPriv->and, pPriv->xor, bgand, bgxor);
        }
    }
    fbFinishAccess(pDrawable);
}

void
fbPushImage(DrawablePtr pDrawable,
            GCPtr pGC,
            FbStip * src,
            FbStride srcStride, int srcX, int x, int y, int width, int height)
{
    RegionPtr pClip = fbGetCompositeClip(pGC);
    int nbox;
    BoxPtr pbox;
    int x1, y1, x2, y2;

    for (nbox = RegionNumRects(pClip),
         pbox = RegionRects(pClip); nbox--; pbox++) {
        x1 = x;
        y1 = y;
        x2 = x + width;
        y2 = y + height;
        if (x1 < pbox->x1)
            x1 = pbox->x1;
        if (y1 < pbox->y1)
            y1 = pbox->y1;
        if (x2 > pbox->x2)
            x2 = pbox->x2;
        if (y2 > pbox->y2)
            y2 = pbox->y2;
        if (x1 >= x2 || y1 >= y2)
            continue;
        fbPushFill(pDrawable,
                   pGC,
                   src + (y1 - y) * srcStride,
                   srcStride, srcX + (x1 - x), x1, y1, x2 - x1, y2 - y1);
    }
}

void
fbPushPixels(GCPtr pGC,
             PixmapPtr pBitmap,
             DrawablePtr pDrawable, int dx, int dy, int xOrg, int yOrg)
{
    FbStip *stip;
    FbStride stipStride;
    int stipBpp;
    _X_UNUSED int stipXoff, stipYoff;

    fbGetStipDrawable(&pBitmap->drawable, stip, stipStride, stipBpp, stipXoff,
                      stipYoff);

    fbPushImage(pDrawable, pGC, stip, stipStride, 0, xOrg, yOrg, dx, dy);
}
