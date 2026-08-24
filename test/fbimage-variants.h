/*
 * test/fbimage-variants.h
 
 * Header-only scalar vs vector for fbGetImage pm &= loop.
 */

#ifndef FBIMAGE_VARIANTS_H
#define FBIMAGE_VARIANTS_H

#include <stdint.h>
#include <string.h>

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

typedef uint32_t FbStip;
typedef uint32_t FbBits;

static void
fbimage_pm_scalar(FbStip *dst, int len, FbBits pm)
{
    for (int i = 0; i < len; i++)
        dst[i] &= pm;
}

#if __has_attribute(vector_size)
typedef FbStip FbVec4 __attribute__((vector_size(16)));
static inline FbVec4 FbimageVecLoad(const FbStip *p) { FbVec4 v; __builtin_memcpy(&v, p, sizeof(v)); return v; }
static inline void FbimageVecStore(FbStip *p, FbVec4 v) { __builtin_memcpy(p, &v, sizeof(v)); }

static void
fbimage_pm_vector(FbStip *dst, int len, FbBits pm)
{
    if (pm == (FbBits)-1)
        return;
    int n = len & ~3;
    if (n >= 4) {
        FbVec4 vpm = {pm, pm, pm, pm};
        FbStip *d = dst;
        int vn = n;
        while (vn >= 4) {
            FbVec4 v = FbimageVecLoad(d);
            v &= vpm;
            FbimageVecStore(d, v);
            d += 4;
            vn -= 4;
        }
    }
    for (int i = n; i < len; i++)
        dst[i] &= pm;
}
#else
static void
fbimage_pm_vector(FbStip *dst, int len, FbBits pm)
{
    fbimage_pm_scalar(dst, len, pm);
}
#endif

#endif
