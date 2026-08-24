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

#include <string.h>
#include "os/osdep.h"
#include "fb.h"
#include "fbvec.h"

#define InitializeShifts(sx,dx,ls,rs) { \
    if (sx != dx) { \
	if (sx > dx) { \
	    ls = sx - dx; \
	    rs = FB_UNIT - ls; \
	} else { \
	    rs = dx - sx; \
	    ls = FB_UNIT - rs; \
	} \
    } \
}

void
fbBlt(FbBits * srcLine,
      FbStride srcStride,
      int srcX,
      FbBits * dstLine,
      FbStride dstStride,
      int dstX,
      int width,
      int height, int alu, FbBits pm, int bpp, Bool reverse, Bool upsidedown)
{
    FbBits *src, *dst;
    int leftShift, rightShift;
    FbBits startmask, endmask;
    FbBits bits, bits1;
    int n, nmiddle;
    Bool destInvarient;
    int startbyte, endbyte;

    FbDeclareMergeRop();

    if (alu == GXcopy && pm == FB_ALLONES &&
        !(srcX & 7) && !(dstX & 7) && !(width & 7))
    {
        CARD8           *src_byte = (CARD8 *) srcLine + (srcX >> 3);
        CARD8           *dst_byte = (CARD8 *) dstLine + (dstX >> 3);
        FbStride        src_byte_stride = srcStride << (FB_SHIFT - 3);
        FbStride        dst_byte_stride = dstStride << (FB_SHIFT - 3);
        int             width_byte = (width >> 3);

        /* Make sure there's no overlap; we can't use memcpy in that
         * case as it's not well defined, so fall through to the
         * general code
         */
        if (src_byte + width_byte <= dst_byte ||
            dst_byte + width_byte <= src_byte)
        {
            int i;

            if (!upsidedown)
                for (i = 0; i < height; i++)
                    MEMCPY_WRAPPED(dst_byte + i * dst_byte_stride,
                                   src_byte + i * src_byte_stride,
                                   width_byte);
            else
                for (i = height - 1; i >= 0; i--)
                    MEMCPY_WRAPPED(dst_byte + i * dst_byte_stride,
                                   src_byte + i * src_byte_stride,
                                   width_byte);

            return;
        }
    }

    FbInitializeMergeRop(alu, pm);
    destInvarient = FbDestInvarientMergeRop();
    if (upsidedown) {
        srcLine += (height - 1) * (srcStride);
        dstLine += (height - 1) * (dstStride);
        srcStride = -srcStride;
        dstStride = -dstStride;
    }
    FbMaskBitsBytes(dstX, width, destInvarient, startmask, startbyte,
                    nmiddle, endmask, endbyte);
    if (reverse) {
        srcLine += ((srcX + width - 1) >> FB_SHIFT) + 1;
        dstLine += ((dstX + width - 1) >> FB_SHIFT) + 1;
        srcX = (srcX + width - 1) & FB_MASK;
        dstX = (dstX + width - 1) & FB_MASK;
    }
    else {
        srcLine += srcX >> FB_SHIFT;
        dstLine += dstX >> FB_SHIFT;
        srcX &= FB_MASK;
        dstX &= FB_MASK;
    }
    if (srcX == dstX) {
        while (height--) {
            src = srcLine;
            srcLine += srcStride;
            dst = dstLine;
            dstLine += dstStride;
            if (reverse) {
                if (endmask) {
                    bits = READ(--src);
                    --dst;
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--)
                        WRITE(--dst, FbDoDestInvarientMergeRop(READ(--src)));
                }
                else {
                    while (n--) {
                        bits = READ(--src);
                        --dst;
                        WRITE(dst, FbDoMergeRop(bits, READ(dst)));
                    }
                }
                if (startmask) {
                    bits = READ(--src);
                    --dst;
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                }
            }
            else {
                if (startmask) {
                    bits = READ(src++);
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                    dst++;
                }
                n = nmiddle;
                if (destInvarient) {
#if __has_attribute(vector_size)
#ifndef FB_ACCESS_WRAPPER
                    if (n >= 4) {
                        FbVec4 vca2 = { _ca2, _ca2, _ca2, _ca2 };
                        FbVec4 vcx2 = { _cx2, _cx2, _cx2, _cx2 };
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vs = FbVecLoad(src);
                            FbVec4 vd = FbDoDestInvariantVec(vs, vca2, vcx2);
                            FbVecStore(dst, vd);
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
#endif
#endif
                    while (n--)
                        WRITE(dst++, FbDoDestInvarientMergeRop(READ(src++)));
                }
                else {
#if __has_attribute(vector_size)
#ifndef FB_ACCESS_WRAPPER
                    if (n >= 4) {
                        FbVec4 vca1 = { _ca1, _ca1, _ca1, _ca1 };
                        FbVec4 vcx1 = { _cx1, _cx1, _cx1, _cx1 };
                        FbVec4 vca2 = { _ca2, _ca2, _ca2, _ca2 };
                        FbVec4 vcx2 = { _cx2, _cx2, _cx2, _cx2 };
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vs = FbVecLoad(src);
                            FbVec4 vd = FbVecLoad(dst);
                            FbVec4 vr = FbDoMergeRopVec(vs, vd, vca1, vcx1, vca2, vcx2);
                            FbVecStore(dst, vr);
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
#endif
#endif
                    while (n--) {
                        bits = READ(src++);
                        WRITE(dst, FbDoMergeRop(bits, READ(dst)));
                        dst++;
                    }
                }
                if (endmask) {
                    bits = READ(src);
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
            }
        }
    }
    else {
        if (srcX > dstX) {
            leftShift = srcX - dstX;
            rightShift = FB_UNIT - leftShift;
        }
        else {
            rightShift = dstX - srcX;
            leftShift = FB_UNIT - rightShift;
        }
        while (height--) {
            src = srcLine;
            srcLine += srcStride;
            dst = dstLine;
            dstLine += dstStride;

            bits1 = 0;
            if (reverse) {
                if (srcX < dstX)
                    bits1 = READ(--src);
                if (endmask) {
                    bits = FbScrRight(bits1, rightShift);
                    if (FbScrRight(endmask, leftShift)) {
                        bits1 = READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                    }
                    --dst;
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--) {
                        bits = FbScrRight(bits1, rightShift);
                        bits1 = READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                        --dst;
                        WRITE(dst, FbDoDestInvarientMergeRop(bits));
                    }
                }
                else {
                    while (n--) {
                        bits = FbScrRight(bits1, rightShift);
                        bits1 = READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                        --dst;
                        WRITE(dst, FbDoMergeRop(bits, READ(dst)));
                    }
                }
                if (startmask) {
                    bits = FbScrRight(bits1, rightShift);
                    if (FbScrRight(startmask, leftShift)) {
                        bits1 = READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                    }
                    --dst;
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                }
            }
            else {
                if (srcX > dstX)
                    bits1 = READ(src++);
                if (startmask) {
                    bits = FbScrLeft(bits1, leftShift);
                    if (FbScrLeft(startmask, rightShift)) {
                        bits1 = READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                    }
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                    dst++;
                }
                n = nmiddle;
                if (destInvarient) {
#if __has_attribute(vector_size)
#ifndef FB_ACCESS_WRAPPER
                    if (n >= 4) {
                        FbVec4 vca2 = { _ca2, _ca2, _ca2, _ca2 };
                        FbVec4 vcx2 = { _cx2, _cx2, _cx2, _cx2 };
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vcurr = FbVecLoad(src);
                            FbVec4 vprev = { bits1, vcurr[0], vcurr[1], vcurr[2] };
                            FbVec4 vbits = FbVecScrLeft(vprev, leftShift) |
                                           FbVecScrRight(vcurr, rightShift);
                            FbVec4 vr = FbDoDestInvariantVec(vbits, vca2, vcx2);
                            FbVecStore(dst, vr);
                            bits1 = vcurr[3];
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
#endif
#endif
                    while (n--) {
                        bits = FbScrLeft(bits1, leftShift);
                        bits1 = READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                        WRITE(dst, FbDoDestInvarientMergeRop(bits));
                        dst++;
                    }
                }
                else {
#if __has_attribute(vector_size)
#ifndef FB_ACCESS_WRAPPER
                    if (n >= 4) {
                        FbVec4 vca1 = { _ca1, _ca1, _ca1, _ca1 };
                        FbVec4 vcx1 = { _cx1, _cx1, _cx1, _cx1 };
                        FbVec4 vca2 = { _ca2, _ca2, _ca2, _ca2 };
                        FbVec4 vcx2 = { _cx2, _cx2, _cx2, _cx2 };
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vcurr = FbVecLoad(src);
                            FbVec4 vprev = { bits1, vcurr[0], vcurr[1], vcurr[2] };
                            FbVec4 vbits = FbVecScrLeft(vprev, leftShift) |
                                           FbVecScrRight(vcurr, rightShift);
                            FbVec4 vd = FbVecLoad(dst);
                            FbVec4 vr = FbDoMergeRopVec(vbits, vd, vca1, vcx1, vca2, vcx2);
                            FbVecStore(dst, vr);
                            bits1 = vcurr[3];
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
#endif
#endif
                    while (n--) {
                        bits = FbScrLeft(bits1, leftShift);
                        bits1 = READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                        WRITE(dst, FbDoMergeRop(bits, READ(dst)));
                        dst++;
                    }
                }
                if (endmask) {
                    bits = FbScrLeft(bits1, leftShift);
                    if (FbScrLeft(endmask, rightShift)) {
                        bits1 = READ(src);
                        bits |= FbScrRight(bits1, rightShift);
                    }
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
            }
        }
    }
}

void
fbBltStip(FbStip * src, FbStride srcStride,     /* in FbStip units, not FbBits units */
          int srcX, FbStip * dst, FbStride dstStride,   /* in FbStip units, not FbBits units */
          int dstX, int width, int height, int alu, FbBits pm, int bpp)
{
    fbBlt((FbBits *) src, FbStipStrideToBitsStride(srcStride), srcX,
          (FbBits *) dst, FbStipStrideToBitsStride(dstStride), dstX,
          width, height, alu, pm, bpp, FALSE, FALSE);
}
