/*
 * test/fbtile-variants.h
 *
 * Header-only scalar vs vector variants of fbEvenTile.
 */

#ifndef FBTILE_VARIANTS_H
#define FBTILE_VARIANTS_H

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
    { n = (w); lb = 0; rb = 0; r = FbRightMask((x) + n); if (r) { if ((copy) && (((x) + n) & 7) == 0) rb = (((x) + n) & FB_MASK) >> 3; else rb = FbByteMaskInvalid; } \
      l = FbLeftMask(x); if (l) { if ((copy) && ((x) & 7) == 0) lb = ((x) & FB_MASK) >> 3; else lb = FbByteMaskInvalid; n -= FB_UNIT - ((x) & FB_MASK); if (n < 0) { if (lb != FbByteMaskInvalid) { if (rb == FbByteMaskInvalid) lb = FbByteMaskInvalid; else if (rb) { lb |= (rb - lb) << (FB_SHIFT - 3); rb = 0; } } n = 0; l &= r; r = 0; } } n >>= FB_SHIFT; }
#define FbDoLeftMaskByteRRop(dst, lb, l, and, xor) { (void)(lb); *(dst) = FbDoMaskRRop(*(dst), and, xor, l); }
#define FbDoRightMaskByteRRop(dst, rb, r, and, xor) { (void)(rb); *(dst) = FbDoMaskRRop(*(dst), and, xor, r); }

#define FbRotLeft(x,n) (FbScrLeft(x,n) | ((n) ? FbScrRight(x, FB_UNIT - (n)) : 0))
#define FbRotRight(x,n) (FbScrRight(x,n) | ((n) ? FbScrLeft(x, FB_UNIT - (n)) : 0))

#define fbFillFromBit(b,t) (~((t)((b) & 1) - 1))
#define fbXorT(rop,fg,pm,t) ((((fg) & fbFillFromBit((rop) >> 1,t)) | (~(fg) & fbFillFromBit((rop) >> 3,t))) & (pm))
#define fbAndT(rop,fg,pm,t) ((((fg) & fbFillFromBit(rop ^ (rop>>1),t)) | (~(fg) & fbFillFromBit((rop>>2) ^ (rop>>3),t))) | ~(pm))
#define fbXor(rop,fg,pm) fbXorT(rop,fg,pm,FbBits)
#define fbAnd(rop,fg,pm) fbAndT(rop,fg,pm,FbBits)
#define FbDestInvarientRop(alu,pm) ((pm) == FB_ALLONES && (((alu) >> 1 & 5) == ((alu) & 5)))

#define modulus(a,b,m) ((m) = (a) % (b), (m) < 0 ? (m) += (b) : 0)

static void
fbtile_variant_scalar(FbBits *dst, FbStride dstStride, int dstX,
                      int width, int height,
                      FbBits *tile, FbStride tileStride,
                      int tileHeight, int alu, FbBits pm,
                      int xRot, int yRot)
{
    FbBits *t, *tileEnd, bits;
    FbBits startmask, endmask;
    FbBits and, xor;
    int n, nmiddle;
    int tileX, tileY;
    int rot;
    int startbyte, endbyte;
    dst += dstX >> FB_SHIFT;
    dstX &= FB_MASK;
    FbMaskBitsBytes(dstX, width, FbDestInvarientRop(alu, pm), startmask, startbyte, nmiddle, endmask, endbyte);
    if (startmask)
        dstStride--;
    dstStride -= nmiddle;
    tileEnd = tile + tileHeight * tileStride;
    modulus(-yRot, tileHeight, tileY);
    t = tile + tileY * tileStride;
    modulus(-xRot, FB_UNIT, tileX);
    rot = tileX;
    while (height--) {
        bits = *t;
        t += tileStride;
        if (t >= tileEnd)
            t = tile;
        bits = FbRotLeft(bits, rot);
        and = fbAnd(alu, bits, pm);
        xor = fbXor(alu, bits, pm);
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
static inline FbVec4 FbtileVecLoad(const FbBits *p) { FbVec4 v; __builtin_memcpy(&v, p, sizeof(v)); return v; }
static inline void FbtileVecStore(FbBits *p, FbVec4 v) { __builtin_memcpy(p, &v, sizeof(v)); }
static inline FbVec4 FbtileDoRRopVec(FbVec4 d, FbVec4 a, FbVec4 x) { return (d & a) ^ x; }

static void
fbtile_variant_vector(FbBits *dst, FbStride dstStride, int dstX,
                      int width, int height,
                      FbBits *tile, FbStride tileStride,
                      int tileHeight, int alu, FbBits pm,
                      int xRot, int yRot)
{
    FbBits *t, *tileEnd, bits;
    FbBits startmask, endmask;
    FbBits and, xor;
    int n, nmiddle;
    int tileX, tileY;
    int rot;
    int startbyte, endbyte;
    dst += dstX >> FB_SHIFT;
    dstX &= FB_MASK;
    FbMaskBitsBytes(dstX, width, FbDestInvarientRop(alu, pm), startmask, startbyte, nmiddle, endmask, endbyte);
    if (startmask)
        dstStride--;
    dstStride -= nmiddle;
    tileEnd = tile + tileHeight * tileStride;
    modulus(-yRot, tileHeight, tileY);
    t = tile + tileY * tileStride;
    modulus(-xRot, FB_UNIT, tileX);
    rot = tileX;
    while (height--) {
        bits = *t;
        t += tileStride;
        if (t >= tileEnd)
            t = tile;
        bits = FbRotLeft(bits, rot);
        and = fbAnd(alu, bits, pm);
        xor = fbXor(alu, bits, pm);
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
                    FbtileVecStore(dst, vxor);
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
                    FbVec4 vd = FbtileVecLoad(dst);
                    FbtileVecStore(dst, FbtileDoRRopVec(vd, vand, vxor));
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
fbtile_variant_vector(FbBits *dst, FbStride dstStride, int dstX,
                      int width, int height,
                      FbBits *tile, FbStride tileStride,
                      int tileHeight, int alu, FbBits pm,
                      int xRot, int yRot)
{
    fbtile_variant_scalar(dst, dstStride, dstX, width, height, tile, tileStride, tileHeight, alu, pm, xRot, yRot);
}
#endif

#endif
