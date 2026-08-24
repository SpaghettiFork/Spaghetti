/*
 * test/fbpolyseg-variants.h
 
 * Header-only scalar vs vector for POLYSEGMENT hline fast path.
 */

#ifndef FBPOLYSEG_VARIANTS_H
#define FBPOLYSEG_VARIANTS_H

#include <stdint.h>
#include <string.h>

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#define FB_SHIFT 5
#define FB_UNIT (1 << FB_SHIFT)
#define FB_MASK (FB_UNIT - 1)
#define FB_ALLONES ((FbBits)-1)
typedef uint32_t FbBits;
typedef int FbStride;

#define FbDoRRop(dst, and, xor) (((dst) & (and)) ^ (xor))
#define FbDoMaskRRop(dst, and, xor, mask) (((dst) & ((and) | ~(mask))) ^ ((xor) & (mask)))

static inline void
polyseg_hline_scalar(FbBits *dstLine, int nmiddle, FbBits andBits, FbBits xorBits, FbBits startmask, FbBits endmask)
{
    if (startmask) {
        *dstLine = FbDoMaskRRop(*dstLine, andBits, xorBits, startmask);
        dstLine++;
    }
    if (!andBits) {
        while (nmiddle--)
            *dstLine++ = xorBits;
    } else {
        while (nmiddle--) {
            *dstLine = FbDoRRop(*dstLine, andBits, xorBits);
            dstLine++;
        }
    }
    if (endmask)
        *dstLine = FbDoMaskRRop(*dstLine, andBits, xorBits, endmask);
}

#if __has_attribute(vector_size)
typedef FbBits FbVec4 __attribute__((vector_size(16)));
static inline FbVec4 PolysegVecLoad(const FbBits *p) { FbVec4 v; __builtin_memcpy(&v, p, sizeof(v)); return v; }
static inline void PolysegVecStore(FbBits *p, FbVec4 v) { __builtin_memcpy(p, &v, sizeof(v)); }
static inline FbVec4 PolysegDoRRopVec(FbVec4 d, FbVec4 a, FbVec4 x) { return (d & a) ^ x; }

static inline void
polyseg_hline_vector(FbBits *dstLine, int nmiddle, FbBits andBits, FbBits xorBits, FbBits startmask, FbBits endmask)
{
    if (startmask) {
        *dstLine = FbDoMaskRRop(*dstLine, andBits, xorBits, startmask);
        dstLine++;
    }
    int n = nmiddle;
    if (!andBits) {
        if (n >= 4) {
            FbVec4 vxor = {xorBits, xorBits, xorBits, xorBits};
            int vn = n & ~3;
            while (vn >= 4) {
                PolysegVecStore(dstLine, vxor);
                dstLine += 4;
                vn -= 4;
                n -= 4;
            }
        }
        while (n--)
            *dstLine++ = xorBits;
    } else {
        if (n >= 4) {
            FbVec4 vand = {andBits, andBits, andBits, andBits};
            FbVec4 vxor = {xorBits, xorBits, xorBits, xorBits};
            int vn = n & ~3;
            while (vn >= 4) {
                FbVec4 vd = PolysegVecLoad(dstLine);
                PolysegVecStore(dstLine, PolysegDoRRopVec(vd, vand, vxor));
                dstLine += 4;
                vn -= 4;
                n -= 4;
            }
        }
        while (n--) {
            *dstLine = FbDoRRop(*dstLine, andBits, xorBits);
            dstLine++;
        }
    }
    if (endmask)
        *dstLine = FbDoMaskRRop(*dstLine, andBits, xorBits, endmask);
}
#else
static inline void
polyseg_hline_vector(FbBits *dstLine, int nmiddle, FbBits andBits, FbBits xorBits, FbBits startmask, FbBits endmask)
{
    polyseg_hline_scalar(dstLine, nmiddle, andBits, xorBits, startmask, endmask);
}
#endif

#endif
