/**
 * Spaghetti Display Server
 * Copyright (C) 2026  SpaghettiFork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FBVEC_H
#define FBVEC_H

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#define FB_VEC_THRESH_STORE 8
#define FB_VEC_THRESH_ROP 4

#if __has_attribute(vector_size)

/* 128-bit vector of FbBits (4 x 32 = 16 bytes) */
typedef FbBits FbVec4 __attribute__((vector_size(16)));
typedef FbStip FbStipVec4 __attribute__((vector_size(16)));

static inline FbVec4
FbVecLoad(const FbBits *p)
{
    FbVec4 v;

    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}

static inline void
FbVecStore(FbBits *p, FbVec4 v)
{
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline FbStipVec4
FbStipVecLoad(const FbStip *p)
{
    FbStipVec4 v;

    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}

static inline void
FbStipVecStore(FbStip *p, FbStipVec4 v)
{
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline FbVec4
FbDoRRopVec(FbVec4 d, FbVec4 a, FbVec4 x)
{
    return (d & a) ^ x;
}

static inline FbVec4
FbDoMergeRopVec(FbVec4 src, FbVec4 dst,
                FbVec4 ca1, FbVec4 cx1, FbVec4 ca2, FbVec4 cx2)
{
    return (dst & ((src & ca1) ^ cx1)) ^ ((src & ca2) ^ cx2);
}

static inline FbVec4
FbDoDestInvariantVec(FbVec4 src, FbVec4 ca2, FbVec4 cx2)
{
    return (src & ca2) ^ cx2;
}

#if BITMAP_BIT_ORDER == LSBFirst
#define FbVecScrLeft(v,n)  ((v) >> (n))
#define FbVecScrRight(v,n) ((v) << (n))
#else
#define FbVecScrLeft(v,n)  ((v) << (n))
#define FbVecScrRight(v,n) ((v) >> (n))
#endif

#endif /* __has_attribute(vector_size) */

#endif /* FBVEC_H */
