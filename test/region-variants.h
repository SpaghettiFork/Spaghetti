/*
 * test/region-variants.h
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

#ifndef REGION_VARIANTS_H
#define REGION_VARIANTS_H

/*
 * Copies of the RegionSetExtents scan from dix/region.c, rewritten to
 * operate on a bare [x1]..[x2] accumulator pair so that the scalar and
 * the SIMD variants can be exercised side by side for correctness and
 * for benchmarking.
 *
 * pBox does not need to be 16-byte aligned: the SIMD code loads via
 * memcpy for the same reason dix/region.c does (region rects are only
 * 8-byte aligned).
 */
static void
region_variant_scalar(short *bx1, short *bx2, BoxPtr pBox, BoxPtr pBoxEnd)
{
    while (pBox <= pBoxEnd) {
        if (pBox->x1 < *bx1)
            *bx1 = pBox->x1;
        if (pBox->x2 > *bx2)
            *bx2 = pBox->x2;
        pBox++;
    }
}

#if __has_attribute(vector_size)
static void
region_variant_two_box(short *bx1, short *bx2, BoxPtr pBox, BoxPtr pBoxEnd)
{
    typedef short v8hi __attribute__((vector_size(16)));
    short *r = (short *) pBox;
    short *rEnd = (short *) pBoxEnd + 4;
    v8hi accMin = {
        SHRT_MAX, SHRT_MAX, SHRT_MAX, SHRT_MAX,
        SHRT_MAX, SHRT_MAX, SHRT_MAX, SHRT_MAX
    };
    v8hi accMax = {
        SHRT_MIN, SHRT_MIN, SHRT_MIN, SHRT_MIN,
        SHRT_MIN, SHRT_MIN, SHRT_MIN, SHRT_MIN
    };

    /* each 16-byte vector holds two boxes: [x1,y1,x2,y2, x1,y1,x2,y2] */
    while (r + 8 <= rEnd) {
        v8hi v;

        /* region rects are only 8-byte aligned, so load via memcpy */
        __builtin_memcpy(&v, r, sizeof(v));
        v8hi minMask = v < accMin;
        v8hi maxMask = v > accMax;

        accMin = (v & minMask) | (accMin & ~minMask);
        accMax = (v & maxMask) | (accMax & ~maxMask);
        r += 8;
    }

    /* x1 lives in lanes 0 and 4, x2 in lanes 2 and 6 */
    if (accMin[0] < *bx1)
        *bx1 = accMin[0];
    if (accMin[4] < *bx1)
        *bx1 = accMin[4];
    if (accMax[2] > *bx2)
        *bx2 = accMax[2];
    if (accMax[6] > *bx2)
        *bx2 = accMax[6];

    /* handle the leftover rectangle, if any */
    for (; r < rEnd; r += 4) {
        BoxPtr b = (BoxPtr) r;

        if (b->x1 < *bx1)
            *bx1 = b->x1;
        if (b->x2 > *bx2)
            *bx2 = b->x2;
    }
}

static void
region_variant_four_box(short *bx1, short *bx2, BoxPtr pBox, BoxPtr pBoxEnd)
{
    typedef short v8hi __attribute__((vector_size(16)));
    short *r = (short *) pBox;
    short *rEnd = (short *) pBoxEnd + 4;
    v8hi accMin = {
        SHRT_MAX, SHRT_MIN, SHRT_MAX, SHRT_MIN,
        SHRT_MAX, SHRT_MIN, SHRT_MAX, SHRT_MIN
    };
    v8hi accMax = {
        SHRT_MAX, SHRT_MIN, SHRT_MAX, SHRT_MIN,
        SHRT_MAX, SHRT_MIN, SHRT_MAX, SHRT_MIN
    };

    /* Each 16-byte vector holds two boxes: [x1,y1,x2,y2, x1,y1,x2,y2].
     * Pack the x1 of four boxes into the even lanes and the x2 of the
     * same four boxes into the odd lanes, so accMin (even lanes) tracks
     * the x1 minimum and accMax (odd lanes) the x2 maximum.  The lanes
     * of the wrong polarity sit at sentinels and never win the compare.
     */
    while (r + 16 <= rEnd) {
        v8hi v1, v2, p;

        /* region rects are only 8-byte aligned, so load via memcpy */
        __builtin_memcpy(&v1, r, sizeof(v1));
        __builtin_memcpy(&v2, r + 8, sizeof(v2));
#if defined(__has_builtin) && __has_builtin(__builtin_shufflevector)
        p = __builtin_shufflevector(v1, v2, 0, 2, 8, 10, 4, 6, 12, 14);
#else
        p = __builtin_shuffle(v1, v2, (const v8hi) { 0, 2, 8, 10, 4, 6, 12, 14 });
#endif
        v8hi minMask = p < accMin;
        v8hi maxMask = p > accMax;

        accMin = (p & minMask) | (accMin & ~minMask);
        accMax = (p & maxMask) | (accMax & ~maxMask);
        r += 16;
    }

    /* x1 lives in the even lanes, x2 in the odd lanes */
    if (accMin[0] < *bx1)
        *bx1 = accMin[0];
    if (accMin[2] < *bx1)
        *bx1 = accMin[2];
    if (accMin[4] < *bx1)
        *bx1 = accMin[4];
    if (accMin[6] < *bx1)
        *bx1 = accMin[6];
    if (accMax[1] > *bx2)
        *bx2 = accMax[1];
    if (accMax[3] > *bx2)
        *bx2 = accMax[3];
    if (accMax[5] > *bx2)
        *bx2 = accMax[5];
    if (accMax[7] > *bx2)
        *bx2 = accMax[7];

    /* handle the leftover rectangles, if any */
    for (; r < rEnd; r += 4) {
        BoxPtr b = (BoxPtr) r;

        if (b->x1 < *bx1)
            *bx1 = b->x1;
        if (b->x2 > *bx2)
            *bx2 = b->x2;
    }
}
#endif /* __has_attribute(vector_size) */

#endif /* REGION_VARIANTS_H */