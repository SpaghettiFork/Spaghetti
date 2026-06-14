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

#include <stdlib.h>

#include "fb.h"

PixmapPtr
fbCreatePixmap(ScreenPtr pScreen, int width, int height, int depth,
               unsigned usage_hint)
{
    PixmapPtr pPixmap;
    size_t datasize;
    size_t paddedWidth;
    int adjust;
    int base;
    int bpp = BitsPerPixel(depth);

    paddedWidth = ((width * bpp + FB_MASK) >> FB_SHIFT) * sizeof(FbBits);
    if (paddedWidth / 4 > 32767 || height > 32767)
        return NullPixmap;
    datasize = height * paddedWidth;
    base = pScreen->totalPixmapSize;
    adjust = 0;
    if (base & 7)
        adjust = 8 - (base & 7);
    datasize += adjust;
#ifdef FB_DEBUG
    datasize += 2 * paddedWidth;
#endif
    pPixmap = AllocatePixmap(pScreen, datasize);
    if (!pPixmap)
        return NullPixmap;
    pPixmap->drawable.type = DRAWABLE_PIXMAP;
    pPixmap->drawable.class = 0;
    pPixmap->drawable.pScreen = pScreen;
    pPixmap->drawable.depth = depth;
    pPixmap->drawable.bitsPerPixel = bpp;
    pPixmap->drawable.id = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x = 0;
    pPixmap->drawable.y = 0;
    pPixmap->drawable.width = width;
    pPixmap->drawable.height = height;
    pPixmap->devKind = paddedWidth;
    pPixmap->refcnt = 1;
    pPixmap->devPrivate.ptr = (void *) ((char *) pPixmap + base + adjust);
    pPixmap->primary_pixmap = NULL;

#ifdef FB_DEBUG
    pPixmap->devPrivate.ptr =
        (void *) ((char *) pPixmap->devPrivate.ptr + paddedWidth);
    fbInitializeDrawable(&pPixmap->drawable);
#endif

#if defined(COMPOSITE) || defined(ROOTLESS)
    pPixmap->screen_x = 0;
    pPixmap->screen_y = 0;
#endif

    pPixmap->usage_hint = usage_hint;

    return pPixmap;
}

Bool
fbDestroyPixmap(PixmapPtr pPixmap)
{
    if (--pPixmap->refcnt)
        return TRUE;
    FreePixmap(pPixmap);
    return TRUE;
}

#define ADDRECT(reg,r,fr,rx1,ry1,rx2,ry2)			\
if (((rx1) < (rx2)) && ((ry1) < (ry2)) &&			\
    (!((reg)->data->numRects &&					\
       ((r-1)->y1 == (ry1)) &&					\
       ((r-1)->y2 == (ry2)) &&					\
       ((r-1)->x1 <= (rx1)) &&					\
       ((r-1)->x2 >= (rx2)))))					\
{								\
    if ((reg)->data->numRects == (reg)->data->size)		\
    {								\
	RegionRectAlloc(reg, 1);					\
	fr = RegionBoxptr(reg);				\
	r = fr + (reg)->data->numRects;				\
    }								\
    r->x1 = (rx1);						\
    r->y1 = (ry1);						\
    r->x2 = (rx2);						\
    r->y2 = (ry2);						\
    (reg)->data->numRects++;					\
    if(r->x1 < (reg)->extents.x1)				\
	(reg)->extents.x1 = r->x1;				\
    if(r->x2 > (reg)->extents.x2)				\
	(reg)->extents.x2 = r->x2;				\
    r++;							\
}

/*
 * Count consecutive equal bits from the screen-left side of a word.
 * For LSBFirst, screen-left is bit 0; for MSBFirst, bit (FB_UNIT-1).
 */
#if __has_builtin(__builtin_ctz)
# if BITMAP_BIT_ORDER == LSBFirst
#  define FbCountLeftZeros(w) __builtin_ctz(w)
#  define FbCountLeftOnes(w)  __builtin_ctz(~(w))
# else
#  define FbCountLeftZeros(w) __builtin_clz(w)
#  define FbCountLeftOnes(w)  __builtin_clz(~(w))
# endif
#else
static inline int
FbCtz(uint32_t x)
{
    int n = 0;
    if (!(x & 0x0000FFFF)) { n += 16; x >>= 16; }
    if (!(x & 0x000000FF)) { n +=  8; x >>=  8; }
    if (!(x & 0x0000000F)) { n +=  4; x >>=  4; }
    if (!(x & 0x00000003)) { n +=  2; x >>=  2; }
    if (!(x & 0x00000001)) { n +=  1; }
    return n;
}

static inline int
FbClz(uint32_t x)
{
    int n = 0;
    if (!(x & 0xFFFF0000)) { n += 16; x <<= 16; }
    if (!(x & 0xFF000000)) { n +=  8; x <<=  8; }
    if (!(x & 0xF0000000)) { n +=  4; x <<=  4; }
    if (!(x & 0xC0000000)) { n +=  2; x <<=  2; }
    if (!(x & 0x80000000)) { n +=  1; }
    return n;
}
# if BITMAP_BIT_ORDER == LSBFirst
#  define FbCountLeftZeros(w) FbCtz(w)
#  define FbCountLeftOnes(w)  FbCtz((uint32_t) ~(w))
# else
#  define FbCountLeftZeros(w) FbClz(w)
#  define FbCountLeftOnes(w)  FbClz((uint32_t) ~(w))
# endif
#endif

/*
 * Process one FbBits word using transition-skipping instead of
 * bit-by-bit iteration.
 */
static inline void
fb_word_to_rects(RegionPtr pReg, BoxPtr *rects, BoxPtr *FirstRect,
                 FbBits w, int base, int h, Bool *fInBox, int *rx1)
{
    BoxPtr r = *rects;
    int pos = 0;

    if (*fInBox) {
        int first_zero;
        if (w == FB_ALLONES)
            goto done;
        first_zero = FbCountLeftOnes(w);
        ADDRECT(pReg, r, *FirstRect,
                *rx1, h, base + first_zero, h + 1);
        *fInBox = FALSE;
        if (first_zero + 1 < FB_UNIT) {
            w = FbScrLeft(w, first_zero + 1);
            pos = first_zero + 1;
        } else {
            w = 0;
        }
    }

    while (w != 0) {
        int zeros, ones;
        zeros = FbCountLeftZeros(w);
        pos += zeros;
        w = FbScrLeft(w, zeros);

        *rx1 = base + pos;
        *fInBox = TRUE;

        if (w == FB_ALLONES)
            goto done;

        ones = FbCountLeftOnes(w);
        pos += ones;
        w = FbScrLeft(w, ones);

        if (pos < FB_UNIT) {
            ADDRECT(pReg, r, *FirstRect,
                    *rx1, h, base + pos, h + 1);
            *fInBox = FALSE;
        }
    }
done:
    *rects = r;
}

/* Convert bitmap clip mask into clipping region.
 * First, goes through each line and makes boxes by noting the transitions
 * from 0 to 1 and 1 to 0.
 * Then it coalesces the current line with the previous if they have boxes
 * at the same X coordinates.
 */
RegionPtr
fbPixmapToRegion(PixmapPtr pPix)
{
    register RegionPtr pReg;
    FbBits *pw, w;
    int width, h, base, rx1 = 0, crects;
    FbBits *pwLineEnd;
    int irectPrevStart, irectLineStart;
    register BoxPtr prectO, prectN;
    BoxPtr FirstRect, rects, prectLineStart;
    Bool fInBox, fSame;
    FbBits *pwLine;
    int nWidth;

    pReg = RegionCreate(NULL, 1);
    if (!pReg)
        return NullRegion;
    FirstRect = RegionBoxptr(pReg);
    rects = FirstRect;

    fbPrepareAccess(&pPix->drawable);

    pwLine = (FbBits *) pPix->devPrivate.ptr;
    nWidth = pPix->devKind >> (FB_SHIFT - 3);

    width = pPix->drawable.width;
    pReg->extents.x1 = width - 1;
    pReg->extents.x2 = 0;
    irectPrevStart = -1;
    for (h = 0; h < pPix->drawable.height; h++) {
        pw = pwLine;
        pwLine += nWidth;
        irectLineStart = rects - FirstRect;
        /* If the Screen left most bit of the word is set, we're starting in
         * a box */
        w = READ(pw);
#if BITMAP_BIT_ORDER == LSBFirst
        if (w & 1) {
#else
        if (w & ~(FB_ALLONES >> 1)) {
#endif
            fInBox = TRUE;
            rx1 = 0;
        }
        else
            fInBox = FALSE;
        /* Process all words which are fully in the pixmap */
        pwLineEnd = pw + (width >> FB_SHIFT);
        for (base = 0; pw < pwLineEnd; base += FB_UNIT) {
            w = READ(pw++);
            fb_word_to_rects(pReg, &rects, &FirstRect, w, base, h,
                             &fInBox, &rx1);
        }
        if (width & FB_MASK) {
            /* Process final partial word on line */
            w = READ(pw++);
            /* Mask off bits beyond the drawable width */
#if BITMAP_BIT_ORDER == LSBFirst
            w &= ((FbBits) 1 << (width & FB_MASK)) - 1;
#else
            w &= FB_ALLONES << (FB_UNIT - (width & FB_MASK));
#endif
            fb_word_to_rects(pReg, &rects, &FirstRect, w, base, h,
                             &fInBox, &rx1);
        }
        /* If scanline ended with last bit set, end the box */
        if (fInBox) {
            ADDRECT(pReg, rects, FirstRect,
                    rx1, h, base + (width & FB_MASK), h + 1);
            FirstRect = RegionBoxptr(pReg);
        }
        /* if all rectangles on this line have the same x-coords as
         * those on the previous line, then add 1 to all the previous  y2s and
         * throw away all the rectangles from this line
         */
        fSame = FALSE;
        if (irectPrevStart != -1) {
            crects = irectLineStart - irectPrevStart;
            if (crects == ((rects - FirstRect) - irectLineStart)) {
                prectO = FirstRect + irectPrevStart;
                prectN = prectLineStart = FirstRect + irectLineStart;
                fSame = TRUE;
                while (prectO < prectLineStart) {
                    if ((prectO->x1 != prectN->x1) ||
                        (prectO->x2 != prectN->x2)) {
                        fSame = FALSE;
                        break;
                    }
                    prectO++;
                    prectN++;
                }
                if (fSame) {
                    prectO = FirstRect + irectPrevStart;
                    while (prectO < prectLineStart) {
                        prectO->y2 += 1;
                        prectO++;
                    }
                    rects -= crects;
                    pReg->data->numRects -= crects;
                }
            }
        }
        if (!fSame)
            irectPrevStart = irectLineStart;
    }
    if (!pReg->data->numRects)
        pReg->extents.x1 = pReg->extents.x2 = 0;
    else {
        pReg->extents.y1 = RegionBoxptr(pReg)->y1;
        pReg->extents.y2 = RegionEnd(pReg)->y2;
        if (pReg->data->numRects == 1) {
            free(pReg->data);
            pReg->data = (RegDataPtr) NULL;
        }
    }

    fbFinishAccess(&pPix->drawable);
#ifdef DEBUG
    if (!RegionIsValid(pReg))
        FatalError("Assertion failed file %s, line %d: expr\n", __FILE__,
                   __LINE__);
#endif
    return pReg;
}

#ifdef FB_DEBUG

#ifndef WIN32
#include <stdio.h>
#else
#include <dbg.h>
#endif

static Bool
fbValidateBits(FbStip * bits, int stride, FbStip data)
{
    while (stride--) {
        if (*bits != data) {
#ifdef WIN32
            NCD_DEBUG((DEBUG_FAILURE,
                       "fdValidateBits failed at 0x%x (is 0x%x want 0x%x)",
                       bits, *bits, data));
#else
            fprintf(stderr, "fbValidateBits failed\n");
#endif
            return FALSE;
        }
        bits++;
    }
}

void
fbValidateDrawable(DrawablePtr pDrawable)
{
    FbStip *bits, *first, *last;
    int stride, bpp;
    int xoff, yoff;
    int height;
    Bool failed;

    if (pDrawable->type != DRAWABLE_PIXMAP)
        pDrawable = (DrawablePtr) fbGetWindowPixmap(pDrawable);
    fbGetStipDrawable(pDrawable, bits, stride, bpp, xoff, yoff);
    first = bits - stride;
    last = bits + stride * pDrawable->height;
    if (!fbValidateBits(first, stride, FB_HEAD_BITS) ||
        !fbValidateBits(last, stride, FB_TAIL_BITS))
        fbInitializeDrawable(pDrawable);
    fbFinishAccess(pDrawable);
}

void
fbSetBits(FbStip * bits, int stride, FbStip data)
{
    while (stride--)
        *bits++ = data;
}

void
fbInitializeDrawable(DrawablePtr pDrawable)
{
    FbStip *bits, *first, *last;
    int stride, bpp;
    int xoff, yoff;

    fbGetStipDrawable(pDrawable, bits, stride, bpp, xoff, yoff);
    first = bits - stride;
    last = bits + stride * pDrawable->height;
    fbSetBits(first, stride, FB_HEAD_BITS);
    fbSetBits(last, stride, FB_TAIL_BITS);
    fbFinishAccess(pDrawable);
}
#endif                          /* FB_DEBUG */
