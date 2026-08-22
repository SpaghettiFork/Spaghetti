/*
 * test/fbblt-variants.h
 *
 * Header-only scalar vs vector variants of fbBlt for benchmarking.
 */

#ifndef FBBLT_VARIANTS_H
#define FBBLT_VARIANTS_H

#include <stdint.h>
#include <string.h>

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#define FB_SHIFT 5
#define FB_UNIT (1 << FB_SHIFT)
#define FB_MASK (FB_UNIT - 1)
#define FB_ALLONES ((FbBits) - 1)
typedef uint32_t FbBits;
typedef FbBits FbStip;
typedef int FbStride;
typedef int Bool;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define GXclear 0x0
#define GXand 0x1
#define GXandReverse 0x2
#define GXcopy 0x3
#define GXandInverted 0x4
#define GXnoop 0x5
#define GXxor 0x6
#define GXor 0x7

typedef struct {
    FbBits ca1, cx1, ca2, cx2;
} FbMergeRopRec;
static const FbMergeRopRec FbbltMergeRopBits[16] = {
    {0, 0, 0, 0},
    {0xFFFFFFFF, 0, 0, 0},
    {0xFFFFFFFF, 0, 0xFFFFFFFF, 0},
    {0, 0, 0xFFFFFFFF, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0, 0},
    {0, 0xFFFFFFFF, 0, 0},
    {0, 0xFFFFFFFF, 0xFFFFFFFF, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0, 0xFFFFFFFF, 0, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0, 0xFFFFFFFF},
    {0, 0, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0, 0, 0xFFFFFFFF},
    {0, 0, 0, 0xFFFFFFFF}};
#define FbDeclareMergeRop() FbBits _ca1, _cx1, _ca2, _cx2;
#define FbInitializeMergeRop(alu, pm)                                        \
    {                                                                        \
        const FbMergeRopRec *_b = &FbbltMergeRopBits[(alu) & 0xF];           \
        _ca1 = _b->ca1 & (pm);                                               \
        _cx1 = _b->cx1 | ~(pm);                                              \
        _ca2 = _b->ca2 & (pm);                                               \
        _cx2 = _b->cx2 & (pm);                                               \
    }
#define FbDestInvarientMergeRop() (_ca1 == 0 && _cx1 == 0)
#define FbDoMergeRop(src, dst)                                               \
    (((dst) & (((src) & _ca1) ^ _cx1)) ^ (((src) & _ca2) ^ _cx2))
#define FbDoDestInvarientMergeRop(src) (((src) & _ca2) ^ _cx2)
#define FbScrLeft(x, n) ((x) >> (n))
#define FbScrRight(x, n) ((x) << (n))
#define FbLeftMask(x)                                                        \
    (((x) & FB_MASK) ? FbScrRight(FB_ALLONES, (x) & FB_MASK) : 0)
#define FbRightMask(x)                                                       \
    (((FB_UNIT - (x)) & FB_MASK)                                             \
         ? FbScrLeft(FB_ALLONES, (FB_UNIT - (x)) & FB_MASK)                  \
         : 0)
#define FbMaskBitsBytes(x, w, copy, l, lb, n, r, rb)                         \
    {                                                                        \
        n = (w);                                                             \
        lb = 0;                                                              \
        rb = 0;                                                              \
        r = FbRightMask((x) + n);                                            \
        if (r) {                                                             \
            if ((copy) && (((x) + n) & 7) == 0)                              \
                rb = (((x) + n) & FB_MASK) >> 3;                             \
            else                                                             \
                rb = 0x10;                                                   \
        }                                                                    \
        l = FbLeftMask(x);                                                   \
        if (l) {                                                             \
            if ((copy) && ((x) & 7) == 0)                                    \
                lb = ((x) & FB_MASK) >> 3;                                   \
            else                                                             \
                lb = 0x10;                                                   \
            n -= FB_UNIT - ((x) & FB_MASK);                                  \
            if (n < 0) {                                                     \
                if (lb != 0x10) {                                            \
                    if (rb == 0x10)                                          \
                        lb = 0x10;                                           \
                    else if (rb) {                                           \
                        lb |= (rb - lb) << (FB_SHIFT - 3);                   \
                        rb = 0;                                              \
                    }                                                        \
                }                                                            \
                n = 0;                                                       \
                l &= r;                                                      \
                r = 0;                                                       \
            }                                                                \
        }                                                                    \
        n >>= FB_SHIFT;                                                      \
    }
#define FbDoMaskRRop(dst, and, xor, mask)                                    \
    (((dst) & ((and) | ~(mask))) ^ ((xor) & (mask)))
#define FbDoLeftMaskByteRRop(dst, lb, l, and, xor)                           \
    {                                                                        \
        switch (lb) {                                                        \
        default:                                                             \
            *(dst) = FbDoMaskRRop(*(dst), and, xor, l);                      \
            break;                                                           \
        }                                                                    \
    }
#define FbDoRightMaskByteRRop(dst, rb, r, and, xor)                          \
    {                                                                        \
        switch (rb) {                                                        \
        default:                                                             \
            *(dst) = FbDoMaskRRop(*(dst), and, xor, r);                      \
            break;                                                           \
        }                                                                    \
    }
#define FbDoLeftMaskByteMergeRop(dst, src, lb, l)                            \
    {                                                                        \
        FbBits __xor = ((src) & _ca2) ^ _cx2;                                \
        FbDoLeftMaskByteRRop(dst, lb, l, ((src) & _ca1) ^ _cx1, __xor);      \
    }
#define FbDoRightMaskByteMergeRop(dst, src, rb, r)                           \
    {                                                                        \
        FbBits __xor = ((src) & _ca2) ^ _cx2;                                \
        FbDoRightMaskByteRRop(dst, rb, r, ((src) & _ca1) ^ _cx1, __xor);     \
    }
#define FBBLT_READ(p) (*(p))
#define FBBLT_WRITE(p, v) (*(p) = (v))

/* ------------------------------------------------------------------ */
/* Scalar variant */
static void
fbblt_variant_scalar(FbBits *srcLine, FbStride srcStride, int srcX,
                     FbBits *dstLine, FbStride dstStride, int dstX, int width,
                     int height, int alu, FbBits pm, Bool reverse,
                     Bool upsidedown)
{
    FbBits *src, *dst;
    int leftShift = 0, rightShift = 0;
    FbBits startmask, endmask;
    FbBits bits, bits1;
    int n, nmiddle;
    Bool destInvarient;
    int startbyte, endbyte;
    FbDeclareMergeRop();
    FbInitializeMergeRop(alu, pm);
    destInvarient = FbDestInvarientMergeRop();
    if (upsidedown) {
        srcLine += (height - 1) * srcStride;
        dstLine += (height - 1) * dstStride;
        srcStride = -srcStride;
        dstStride = -dstStride;
    }
    FbMaskBitsBytes(dstX, width, destInvarient, startmask, startbyte, nmiddle,
                    endmask, endbyte);
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
                    bits = FBBLT_READ(--src);
                    --dst;
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--)
                        FBBLT_WRITE(--dst, FbDoDestInvarientMergeRop(
                                               FBBLT_READ(--src)));
                }
                else {
                    while (n--) {
                        bits = FBBLT_READ(--src);
                        --dst;
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                    }
                }
                if (startmask) {
                    bits = FBBLT_READ(--src);
                    --dst;
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                }
            }
            else {
                if (startmask) {
                    bits = FBBLT_READ(src++);
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                    dst++;
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--)
                        FBBLT_WRITE(dst++, FbDoDestInvarientMergeRop(
                                               FBBLT_READ(src++)));
                }
                else {
                    while (n--) {
                        bits = FBBLT_READ(src++);
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                        dst++;
                    }
                }
                if (endmask) {
                    bits = FBBLT_READ(src);
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
                    bits1 = FBBLT_READ(--src);
                if (endmask) {
                    bits = FbScrRight(bits1, rightShift);
                    if (FbScrRight(endmask, leftShift)) {
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                    }
                    --dst;
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--) {
                        bits = FbScrRight(bits1, rightShift);
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                        --dst;
                        FBBLT_WRITE(dst, FbDoDestInvarientMergeRop(bits));
                    }
                }
                else {
                    while (n--) {
                        bits = FbScrRight(bits1, rightShift);
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                        --dst;
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                    }
                }
                if (startmask) {
                    bits = FbScrRight(bits1, rightShift);
                    if (FbScrRight(startmask, leftShift)) {
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                    }
                    --dst;
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                }
            }
            else {
                if (srcX > dstX)
                    bits1 = FBBLT_READ(src++);
                if (startmask) {
                    bits = FbScrLeft(bits1, leftShift);
                    if (FbScrLeft(startmask, rightShift)) {
                        bits1 = FBBLT_READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                    }
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                    dst++;
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--) {
                        bits = FbScrLeft(bits1, leftShift);
                        bits1 = FBBLT_READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                        FBBLT_WRITE(dst, FbDoDestInvarientMergeRop(bits));
                        dst++;
                    }
                }
                else {
                    while (n--) {
                        bits = FbScrLeft(bits1, leftShift);
                        bits1 = FBBLT_READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                        dst++;
                    }
                }
                if (endmask) {
                    bits = FbScrLeft(bits1, leftShift);
                    if (FbScrLeft(endmask, rightShift)) {
                        bits1 = FBBLT_READ(src);
                        bits |= FbScrRight(bits1, rightShift);
                    }
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Vector variant */
#if __has_attribute(vector_size)
typedef FbBits FbVec4 __attribute__((vector_size(16)));
static inline FbVec4
FbbltVecLoad(const FbBits *p)
{
    FbVec4 v;
    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}
static inline void
FbbltVecStore(FbBits *p, FbVec4 v)
{
    __builtin_memcpy(p, &v, sizeof(v));
}
#define FbVecScrLeft(v, n) ((v) >> (n))
#define FbVecScrRight(v, n) ((v) << (n))
static inline FbVec4
FbbltDoMergeRopVec(FbVec4 s, FbVec4 d, FbVec4 ca1, FbVec4 cx1, FbVec4 ca2,
                   FbVec4 cx2)
{
    return (d & ((s & ca1) ^ cx1)) ^ ((s & ca2) ^ cx2);
}
static inline FbVec4
FbbltDoDestInvariantVec(FbVec4 s, FbVec4 ca2, FbVec4 cx2)
{
    return (s & ca2) ^ cx2;
}

static void
fbblt_variant_vector(FbBits *srcLine, FbStride srcStride, int srcX,
                     FbBits *dstLine, FbStride dstStride, int dstX, int width,
                     int height, int alu, FbBits pm, Bool reverse,
                     Bool upsidedown)
{
    FbBits *src, *dst;
    int leftShift = 0, rightShift = 0;
    FbBits startmask, endmask;
    FbBits bits, bits1;
    int n, nmiddle;
    Bool destInvarient;
    int startbyte, endbyte;
    FbDeclareMergeRop();
    FbInitializeMergeRop(alu, pm);
    destInvarient = FbDestInvarientMergeRop();
    if (upsidedown) {
        srcLine += (height - 1) * srcStride;
        dstLine += (height - 1) * dstStride;
        srcStride = -srcStride;
        dstStride = -dstStride;
    }
    FbMaskBitsBytes(dstX, width, destInvarient, startmask, startbyte, nmiddle,
                    endmask, endbyte);
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
                    bits = FBBLT_READ(--src);
                    --dst;
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--)
                        FBBLT_WRITE(--dst, FbDoDestInvarientMergeRop(
                                               FBBLT_READ(--src)));
                }
                else {
                    while (n--) {
                        bits = FBBLT_READ(--src);
                        --dst;
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                    }
                }
                if (startmask) {
                    bits = FBBLT_READ(--src);
                    --dst;
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                }
            }
            else {
                if (startmask) {
                    bits = FBBLT_READ(src++);
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                    dst++;
                }
                n = nmiddle;
                if (destInvarient) {
                    if (n >= 4) {
                        FbVec4 vca2 = {_ca2, _ca2, _ca2, _ca2};
                        FbVec4 vcx2 = {_cx2, _cx2, _cx2, _cx2};
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vs = FbbltVecLoad(src);
                            FbVec4 vd =
                                FbbltDoDestInvariantVec(vs, vca2, vcx2);
                            FbbltVecStore(dst, vd);
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
                    while (n--)
                        FBBLT_WRITE(dst++, FbDoDestInvarientMergeRop(
                                               FBBLT_READ(src++)));
                }
                else {
                    if (n >= 4) {
                        FbVec4 vca1 = {_ca1, _ca1, _ca1, _ca1};
                        FbVec4 vcx1 = {_cx1, _cx1, _cx1, _cx1};
                        FbVec4 vca2 = {_ca2, _ca2, _ca2, _ca2};
                        FbVec4 vcx2 = {_cx2, _cx2, _cx2, _cx2};
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vs = FbbltVecLoad(src);
                            FbVec4 vd = FbbltVecLoad(dst);
                            FbVec4 vr = FbbltDoMergeRopVec(vs, vd, vca1, vcx1,
                                                           vca2, vcx2);
                            FbbltVecStore(dst, vr);
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
                    while (n--) {
                        bits = FBBLT_READ(src++);
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                        dst++;
                    }
                }
                if (endmask) {
                    bits = FBBLT_READ(src);
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
                    bits1 = FBBLT_READ(--src);
                if (endmask) {
                    bits = FbScrRight(bits1, rightShift);
                    if (FbScrRight(endmask, leftShift)) {
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                    }
                    --dst;
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
                n = nmiddle;
                if (destInvarient) {
                    while (n--) {
                        bits = FbScrRight(bits1, rightShift);
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                        --dst;
                        FBBLT_WRITE(dst, FbDoDestInvarientMergeRop(bits));
                    }
                }
                else {
                    while (n--) {
                        bits = FbScrRight(bits1, rightShift);
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                        --dst;
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                    }
                }
                if (startmask) {
                    bits = FbScrRight(bits1, rightShift);
                    if (FbScrRight(startmask, leftShift)) {
                        bits1 = FBBLT_READ(--src);
                        bits |= FbScrLeft(bits1, leftShift);
                    }
                    --dst;
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                }
            }
            else {
                if (srcX > dstX)
                    bits1 = FBBLT_READ(src++);
                if (startmask) {
                    bits = FbScrLeft(bits1, leftShift);
                    if (FbScrLeft(startmask, rightShift)) {
                        bits1 = FBBLT_READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                    }
                    FbDoLeftMaskByteMergeRop(dst, bits, startbyte, startmask);
                    dst++;
                }
                n = nmiddle;
                if (destInvarient) {
                    if (n >= 4) {
                        FbVec4 vca2 = {_ca2, _ca2, _ca2, _ca2};
                        FbVec4 vcx2 = {_cx2, _cx2, _cx2, _cx2};
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vcurr = FbbltVecLoad(src);
                            FbVec4 vprev = {bits1, vcurr[0], vcurr[1],
                                            vcurr[2]};
                            FbVec4 vbits = FbVecScrLeft(vprev, leftShift) |
                                           FbVecScrRight(vcurr, rightShift);
                            FbVec4 vr =
                                FbbltDoDestInvariantVec(vbits, vca2, vcx2);
                            FbbltVecStore(dst, vr);
                            bits1 = vcurr[3];
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
                    while (n--) {
                        bits = FbScrLeft(bits1, leftShift);
                        bits1 = FBBLT_READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                        FBBLT_WRITE(dst, FbDoDestInvarientMergeRop(bits));
                        dst++;
                    }
                }
                else {
                    if (n >= 4) {
                        FbVec4 vca1 = {_ca1, _ca1, _ca1, _ca1};
                        FbVec4 vcx1 = {_cx1, _cx1, _cx1, _cx1};
                        FbVec4 vca2 = {_ca2, _ca2, _ca2, _ca2};
                        FbVec4 vcx2 = {_cx2, _cx2, _cx2, _cx2};
                        int vn = n & ~3;
                        while (vn >= 4) {
                            FbVec4 vcurr = FbbltVecLoad(src);
                            FbVec4 vprev = {bits1, vcurr[0], vcurr[1],
                                            vcurr[2]};
                            FbVec4 vbits = FbVecScrLeft(vprev, leftShift) |
                                           FbVecScrRight(vcurr, rightShift);
                            FbVec4 vd = FbbltVecLoad(dst);
                            FbVec4 vr = FbbltDoMergeRopVec(vbits, vd, vca1,
                                                           vcx1, vca2, vcx2);
                            FbbltVecStore(dst, vr);
                            bits1 = vcurr[3];
                            src += 4;
                            dst += 4;
                            vn -= 4;
                            n -= 4;
                        }
                    }
                    while (n--) {
                        bits = FbScrLeft(bits1, leftShift);
                        bits1 = FBBLT_READ(src++);
                        bits |= FbScrRight(bits1, rightShift);
                        FBBLT_WRITE(dst, FbDoMergeRop(bits, FBBLT_READ(dst)));
                        dst++;
                    }
                }
                if (endmask) {
                    bits = FbScrLeft(bits1, leftShift);
                    if (FbScrLeft(endmask, rightShift)) {
                        bits1 = FBBLT_READ(src);
                        bits |= FbScrRight(bits1, rightShift);
                    }
                    FbDoRightMaskByteMergeRop(dst, bits, endbyte, endmask);
                }
            }
        }
    }
}
#else
/* No vector support...alias to scalar so bench still has two symbols. */
static void
fbblt_variant_vector(FbBits *srcLine, FbStride srcStride, int srcX,
                     FbBits *dstLine, FbStride dstStride, int dstX, int width,
                     int height, int alu, FbBits pm, Bool reverse,
                     Bool upsidedown)
{
    fbblt_variant_scalar(srcLine, srcStride, srcX, dstLine, dstStride, dstX,
                         width, height, alu, pm, reverse, upsidedown);
}
#endif /* __has_attribute(vector_size) */

#endif /* FBBLT_VARIANTS_H */
