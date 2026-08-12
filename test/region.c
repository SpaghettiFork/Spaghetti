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

/* Test relies on assert() */
#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "misc.h"
#include "gc.h"
#include "region.h"
#include "region-variants.h"

#include "tests-common.h"

/* Simple deterministic PRNG (xorshift32) so failures are reproducible. */
static uint32_t
region_rng(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/*
 * Run a RegionSetExtents variant over a banded rect array and check that it
 * recomputes the same extents the way RegionSetExtents does: x1 and x2 are
 * seeded from the first and last rect, then scanned across the whole array.
 *
 * Variants are exercised side by side and must all agree with each other and
 * with the true extents of the rects.
 */
static void
region_variant_scan_check(const BoxRec *boxes, int n)
{
    short x1[3], x2[3];
    short ex1 = boxes[0].x1, ex2 = boxes[n - 1].x2;
    int nv = 0, i;

    for (i = 0; i < n; i++) {
        if (boxes[i].x1 < ex1)
            ex1 = boxes[i].x1;
        if (boxes[i].x2 > ex2)
            ex2 = boxes[i].x2;
    }

    x1[nv] = boxes[0].x1;
    x2[nv] = boxes[n - 1].x2;
    region_variant_scalar(&x1[nv], &x2[nv], (BoxPtr) boxes, (BoxPtr) &boxes[n - 1]);
    assert(x1[nv] == ex1 && x2[nv] == ex2);
    nv++;

#if __has_attribute(vector_size)
    x1[nv] = boxes[0].x1;
    x2[nv] = boxes[n - 1].x2;
    region_variant_two_box(&x1[nv], &x2[nv], (BoxPtr) boxes, (BoxPtr) &boxes[n - 1]);
    assert(x1[nv] == ex1 && x2[nv] == ex2);
    nv++;

    x1[nv] = boxes[0].x1;
    x2[nv] = boxes[n - 1].x2;
    region_variant_four_box(&x1[nv], &x2[nv], (BoxPtr) boxes, (BoxPtr) &boxes[n - 1]);
    assert(x1[nv] == ex1 && x2[nv] == ex2);
    nv++;
#endif

    for (i = 1; i < nv; i++)
        assert(x1[i] == x1[0] && x2[i] == x2[0]);
}

/*
 * RegionSetExtents (dix/region.c) is reached through RegionFromRects() with
 * CT_YXBANDED.  It scans the stored rectangles for the minimum x1 and the
 * maximum x2; on builds with vector support this runs through the SIMD helper
 * RegionSetExtentsVector().  These tests feed it YX-banded rectangles and
 * check that the recomputed extents match the true minimum and maximum.
 *
 * The band ranges alternate between the upper and lower half of the x space
 * so that the global minimum x1 and maximum x2 live on interior rectangles
 * rather than the first or last stored one, which would mask a scan that only
 * kept the initial extents.
 */
static void
region_set_extents_multiband(void)
{
    enum { XMIN = -2000, XMAX = 2000, XMID = 0, BANDH = 8 };
    xRectangle rects[64];
    uint32_t seed = 0x12345678;
    int nboxes, nbnd, iter;

    for (iter = 0; iter < 100; iter++) {
        int minx1 = INT_MAX, maxx2 = INT_MIN;
        int n = 0, b;

        nboxes = 2 + (region_rng(&seed) % 62);
        nbnd = 2 * (1 + (region_rng(&seed) % 2));   /* 2 or 4 bands */
        if (nboxes < nbnd)
            nboxes = nbnd;

        for (b = 0; b < nbnd && n < nboxes; b++) {
            int lo, hi;
            int cursor;
            int bandY = b * (BANDH + 1);
            int want = (nboxes - n + (nbnd - b) - 1) / (nbnd - b);

            /* odd bands live in the lower half of the x range, even bands
             * in the upper half */
            if (b & 1) {
                lo = XMIN;
                hi = XMID;
            }
            else {
                lo = XMID;
                hi = XMAX;
            }

            cursor = lo;
            while (n < nboxes && want-- > 0 && cursor < hi) {
                int w = 1 + (int) (region_rng(&seed) % (uint32_t) (hi - cursor));

                rects[n].x = cursor;
                rects[n].y = bandY;
                rects[n].width = w;
                rects[n].height = BANDH;
                if (rects[n].x < minx1)
                    minx1 = rects[n].x;
                if (rects[n].x + w > maxx2)
                    maxx2 = rects[n].x + w;
                cursor = rects[n].x + w + 1;
                n++;
            }
        }

        assert(n > 0);
        assert(minx1 < XMID && maxx2 > XMID);       /* extremes are interior */

        {
            RegionPtr rgn = RegionFromRects(n, rects, CT_YXBANDED);

            assert(!RegionNar(rgn));
            assert(rgn->extents.x1 == minx1);
            assert(rgn->extents.x2 == maxx2);
            assert(rgn->extents.y1 == 0);
            assert(rgn->extents.y2 == (nbnd - 1) * (BANDH + 1) + BANDH);
            RegionDestroy(rgn);
        }
    }
}

/*
 * Exercise the exact rectangle counts that straddle the SIMD loop
 * boundaries (2, 3, 4, 5, 6, 7 and larger) with tightly packed, single-band
 * rectangles so every code path of the extents scan is reached.
 */
static void
region_set_extents_counts(void)
{
    static const int counts[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 33 };
    xRectangle rects[64];
    int c, i;

    for (c = 0; c < ARRAY_SIZE(counts); c++) {
        int n = counts[c];
        int minx1 = INT_MAX, maxx2 = INT_MIN;

        assert(n <= ARRAY_SIZE(rects));
        for (i = 0; i < n; i++) {
            rects[i].x = 2 * i;
            rects[i].y = 0;
            rects[i].width = 2;
            rects[i].height = 8;
            if (rects[i].x < minx1)
                minx1 = rects[i].x;
            if (rects[i].x + 2 > maxx2)
                maxx2 = rects[i].x + 2;
        }

        {
            RegionPtr rgn = RegionFromRects(n, rects, CT_YXBANDED);

            assert(!RegionNar(rgn));
            assert(rgn->extents.x1 == minx1);
            assert(rgn->extents.x2 == maxx2);
            assert(rgn->extents.y1 == 0);
            assert(rgn->extents.y2 == 8);
            RegionDestroy(rgn);
        }
    }
}

static void
region_set_extents_empty(void)
{
    RegionPtr rgn = RegionFromRects(0, NULL, CT_YXBANDED);

    assert(!RegionNar(rgn));
    assert(rgn->extents.x1 == 0 && rgn->extents.x2 == 0);
    assert(rgn->extents.y1 == 0 && rgn->extents.y2 == 0);
    RegionDestroy(rgn);
}

/*
 * Exercise every available RegionSetExtents variant (scalar, 2-box and 4-box)
 * on the same banded rectangle sets used above and verify that they all agree
 * and produce the true extents.  This doubles as a check that the shipped
 * 2-box helper in dix/region.c gives results identical to the scalar scan.
 */
static void
region_set_extents_variants(void)
{
    static const int counts[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 33, 64 };
    BoxRec boxes[64];
    int c, i;

    for (c = 0; c < ARRAY_SIZE(counts); c++) {
        int n = counts[c];

        assert(n <= ARRAY_SIZE(boxes));
        for (i = 0; i < n; i++) {
            boxes[i].x1 = (2 * i) - 8;
            boxes[i].y1 = 0;
            boxes[i].x2 = boxes[i].x1 + 2;
            boxes[i].y2 = 8;
        }
        region_variant_scan_check(boxes, n);
    }

    {
        /* multiband layout similar to region_set_extents_multiband */
        enum { XMIN = -2000, XMAX = 2000, XMID = 0, BANDH = 8 };
        uint32_t seed = 0x12345678;
        int nboxes, nbnd, b, n;

        nboxes = 2 + (region_rng(&seed) % 62);
        nbnd = 2 * (1 + (region_rng(&seed) % 2));
        if (nboxes < nbnd)
            nboxes = nbnd;
        n = 0;
        for (b = 0; b < nbnd && n < nboxes; b++) {
            int lo = (b & 1) ? XMIN : XMID;
            int hi = (b & 1) ? XMID : XMAX;
            int cursor = lo;
            int want = (nboxes - n + (nbnd - b) - 1) / (nbnd - b);

            while (n < nboxes && want-- > 0 && cursor < hi) {
                int w = 1 + (int) (region_rng(&seed) % (uint32_t) (hi - cursor));

                boxes[n].x1 = cursor;
                boxes[n].y1 = b * (BANDH + 1);
                boxes[n].x2 = cursor + w;
                boxes[n].y2 = boxes[n].y1 + BANDH;
                cursor = boxes[n].x2 + 1;
                n++;
            }
        }
        region_variant_scan_check(boxes, n);
    }
}

const testfunc_t *
region_test(void)
{
    static const testfunc_t tests[] = {
        region_set_extents_empty,
        region_set_extents_counts,
        region_set_extents_multiband,
        region_set_extents_variants,
    };
    return tests;
}
