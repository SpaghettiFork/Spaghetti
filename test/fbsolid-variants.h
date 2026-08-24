/*
 * test/fbsolid-variants.h
 *
 * Header-only scalar vs vector variants of fbSolid.
 */

#ifndef FBSOLID_VARIANTS_H
#define FBSOLID_VARIANTS_H

#include <stdint.h>
#include <string.h>

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#define FB_SHIFT 5
#define FB_UNIT (1 << FB_SHIFT)
#define FB_MASK (FB_UNIT - 1)
#define FB_ALLONES ((FbBits) -1)
typedef uint32_t FbBits;
typedef int FbStride;

#define FbDoRRop(dst, and, xor) (((dst) & (and)) ^ (xor))
#define FbDoMaskRRop(dst, and, xor, mask) (((dst) & ((and) | ~(mask))) ^ ((xor) & (mask)))
#define FbScrLeft(x, n) ((x) >> (n))
#define FbScrRight(x, n) ((x) << (n))
#define FbLeftMask(x) (((x) & FB_MASK) ? FbScrRight(FB_ALLONES, (x) & FB_MASK) : 0)
#define FbRightMask(x) (((FB_UNIT - (x)) & FB_MASK) ? FbScrLeft(FB_ALLONES, (FB_UNIT - (x)) & FB_MASK) : 0)
#define FbByteMaskInvalid 0x10
#define FbMaskBitsBytes(x, w, copy, l, lb, n, r, rb) \
    { \
        n = (w); lb = 0; rb = 0; r = FbRightMask((x) + n); \
        if (r) { if ((copy) && (((x) + n) & 7) == 0) rb = (((x) + n) & FB_MASK) >> 3; else rb = FbByteMaskInvalid; } \
        l = FbLeftMask(x); \
        if (l) { if ((copy) && ((x) & 7) == 0) lb = ((x) & FB_MASK) >> 3; else lb = FbByteMaskInvalid; \
            n -= FB_UNIT - ((x) & FB_MASK); \
            if (n < 0) { \
                if (lb != FbByteMaskInvalid) { if (rb == FbByteMaskInvalid) lb = FbByteMaskInvalid; else if (rb) { lb |= (rb - lb) << (FB_SHIFT - 3); rb = 0; } } \
                n = 0; l &= r; r = 0; } } \
        n >>= FB_SHIFT; }
#define FbDoLeftMaskByteRRop(dst, lb, l, and, xor) \
    { (void)(lb); *(dst) = FbDoMaskRRop(*(dst), and, xor, l); }
#define FbDoRightMaskByteRRop(dst, rb, r, and, xor) \
    { (void)(rb); *(dst) = FbDoMaskRRop(*(dst), and, xor, r); }

static void
fbsolid_variant_scalar(FbBits *dst, FbStride dstStride,
                       int dstX, int width, int height,
                       FbBits and, FbBits xor)
{
    FbBits startmask, endmask;
    int n, nmiddle;
    int startbyte, endbyte;
    dst += dstX >> FB_SHIFT;
    dstX &= FB_MASK;
    FbMaskBitsBytes(dstX, width, and == 0, startmask, startbyte, nmiddle, endmask, endbyte);
    if (startmask)
        dstStride--;
    dstStride -= nmiddle;
    while (height--) {
        if (startmask) {
            FbDoLeftMaskByteRRop(dst, startbyte, startmask, and, xor);
            dst++;
        }
        n = nmiddle;
        if (!and) {
            while (n--)
                *dst++ = xor;
        } else {
            while (n--) {
                *dst = FbDoRRop(*dst, and, xor);
                dst++;
            }
        }
        if (endmask)
            FbDoRightMaskByteRRop(dst, endbyte, endmask, and, xor);
        dst += dstStride;
    }
}

#if __has_attribute(vector_size)
typedef FbBits FbVec4 __attribute__((vector_size(16)));

static inline FbVec4
FbsolidVecLoad(const FbBits *p)
{
    FbVec4 v;
    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}

static inline void
FbsolidVecStore(FbBits *p, FbVec4 v)
{
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline FbVec4
FbsolidDoRRopVec(FbVec4 dst, FbVec4 andv, FbVec4 xorv)
{
    return (dst & andv) ^ xorv;
}

static void
fbsolid_variant_vector(FbBits *dst, FbStride dstStride,
                       int dstX, int width, int height,
                       FbBits and, FbBits xor)
{
    FbBits startmask, endmask;
    int n, nmiddle;
    int startbyte, endbyte;
    dst += dstX >> FB_SHIFT;
    dstX &= FB_MASK;
    FbMaskBitsBytes(dstX, width, and == 0, startmask, startbyte, nmiddle, endmask, endbyte);
    if (startmask)
        dstStride--;
    dstStride -= nmiddle;
    while (height--) {
        if (startmask) {
            FbDoLeftMaskByteRRop(dst, startbyte, startmask, and, xor);
            dst++;
        }
        n = nmiddle;
        if (!and) {
            if (n >= 8) {
                FbVec4 vxor = {xor, xor, xor, xor};
                int vn = n & ~3;
                while (vn >= 4) {
                    FbsolidVecStore(dst, vxor);
                    dst += 4;
                    vn -= 4;
                    n -= 4;
                }
            }
            while (n--)
                *dst++ = xor;
        } else {
            if (n >= 4) {
                FbVec4 vand = {and, and, and, and};
                FbVec4 vxor = {xor, xor, xor, xor};
                int vn = n & ~3;
                while (vn >= 4) {
                    FbVec4 vd = FbsolidVecLoad(dst);
                    FbVec4 vr = FbsolidDoRRopVec(vd, vand, vxor);
                    FbsolidVecStore(dst, vr);
                    dst += 4;
                    vn -= 4;
                    n -= 4;
                }
            }
            while (n--) {
                *dst = FbDoRRop(*dst, and, xor);
                dst++;
            }
        }
        if (endmask)
            FbDoRightMaskByteRRop(dst, endbyte, endmask, and, xor);
        dst += dstStride;
    }
}
#else
static void
fbsolid_variant_vector(FbBits *dst, FbStride dstStride,
                       int dstX, int width, int height,
                       FbBits and, FbBits xor)
{
    fbsolid_variant_scalar(dst, dstStride, dstX, width, height, and, xor);
}
#endif

#endif
