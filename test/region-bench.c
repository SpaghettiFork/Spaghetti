/**
 * test/region-bench.c
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

/*
 * On-demand benchmark for the RegionSetExtents scan variants in
 * region-variants.h.  Not run as part of the unit test suite; invoke
 * directly, or via:  meson test --benchmark region-bench
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "misc.h"
#include "gc.h"
#include "region.h"
#include "region-variants.h"

#include "tests-common.h"

#define NBOX 4096
static BoxRec boxes[NBOX + 16];

typedef void (*vfp) (short *, short *, BoxPtr, BoxPtr);

static double
bench_one(vfp f, int n, long reps)
{
    BoxPtr b = boxes + 8;
    short x1, x2;
    long i;
    struct timespec t0, t1;

    for (i = 0; i < 1000; i++) {
        x1 = b[0].x1;
        x2 = b[n - 1].x2;
        f(&x1, &x2, b, &b[n - 1]);
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < reps; i++) {
        x1 = b[0].x1;
        x2 = b[n - 1].x2;
        f(&x1, &x2, b, &b[n - 1]);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / reps;
}

int
main(void)
{
    static const int sizes[] = { 4, 16, 64, 256, 1024, 4096 };
    int i, n;

    for (i = 0; i < NBOX; i++) {
        boxes[i].x1 = (short) (rand() % 60000 - 30000);
        boxes[i].y1 = (short) (rand() % 60000 - 30000);
        boxes[i].x2 = (short) (rand() % 60000 - 30000);
        boxes[i].y2 = (short) (rand() % 60000 - 30000);
    }

    #if __has_attribute(vector_size)
    printf("%-6s %-14s %-14s %-14s\n", "n", "scalar(ns)", "two-box(ns)", "four-box(ns)");
#else
    printf("%-6s %-14s\n", "n", "scalar(ns)");
#endif

    for (i = 0; i < ARRAY_SIZE(sizes); i++) {
        n = sizes[i];
        long reps = 4000000 / n;
        double s = bench_one(region_variant_scalar, n, reps);

        if (reps < 50)
            reps = 50;
#if __has_attribute(vector_size)
        {
            double t = bench_one(region_variant_two_box, n, reps);
            double f = bench_one(region_variant_four_box, n, reps);

            printf("%-6d %-14.2f %-14.2f %-14.2f\n", n, s, t, f);
        }
#else
        {
            double t = bench_one(region_variant_two_box, n, reps);

            printf("%-6d %-14.2f %-14.2f\n", n, s, t);
        }
#endif
    }

    return 0;
}