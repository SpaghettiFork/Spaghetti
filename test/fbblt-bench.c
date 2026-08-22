/**
 * test/fbblt-bench.c
 *
 * Full scalar vs vector benchmark for the fbBlt nmiddle loops.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "fbblt-variants.h"

typedef void (*fbblt_fn)(FbBits *, FbStride, int, FbBits *, FbStride,
                         int, int, int, int, FbBits, Bool, Bool);

static int g_height = 64;
static int g_min_size = 4;

static double
bench_one(fbblt_fn fn, int nmiddle, int srcX, int dstX, int alu, FbBits pm,
          long reps)
{
    int width = nmiddle * FB_UNIT;
    int height = g_height;
    int srcStride = nmiddle + 4;
    int dstStride = nmiddle + 4;
    FbBits *src = calloc((size_t)srcStride * height, sizeof(FbBits));
    FbBits *dst = calloc((size_t)dstStride * height, sizeof(FbBits));
    struct timespec t0, t1;
    long i;
    volatile FbBits sink = 0;

    if (!src || !dst) {
        free(src);
        free(dst);
        return 0;
    }
    for (i = 0; i < (long)srcStride * height; i++)
        src[i] = (FbBits)rand() ^ ((FbBits)rand() << 16);
    for (i = 0; i < (long)dstStride * height; i++)
        dst[i] = (FbBits)rand();

    for (i = 0; i < 200; i++)
        fn(src, srcStride, srcX, dst, dstStride, dstX, width, height, alu, pm,
           FALSE, FALSE);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < reps; i++)
        fn(src, srcStride, srcX, dst, dstStride, dstX, width, height, alu, pm,
           FALSE, FALSE);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    for (i = 0; i < (long)dstStride * height; i++)
        sink ^= dst[i];
    (void)sink;
    free(src);
    free(dst);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) /
           (double)reps;
}

static void
print_table(const char *title, int srcX, int dstX, int alu, FbBits pm)
{
    static const int sizes[] = {4, 16, 64, 256, 1024, 4096};
    int i;
    printf("\n# %s (srcX=%d dstX=%d alu=%d pm=%08x height=%d "
           "width=nmiddle*%d)\n",
           title, srcX, dstX, alu, (unsigned)pm, g_height, FB_UNIT);
#if __has_attribute(vector_size)
    printf("%-8s %-14s %-14s %-10s\n", "nmiddle", "scalar(ns)", "vector(ns)",
           "speedup");
#else
    printf("%-8s %-14s\n", "nmiddle", "scalar(ns)");
#endif
    for (i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++) {
        int n = sizes[i];
        if (n < g_min_size)
            continue;
        int height_factor = g_height > 64 ? g_height / 64 : 1;
        long reps = 200000 / ((n ? n : 1) * height_factor);
        if (reps < 20)
            reps = 20;
        if (reps > 5000)
            reps = 5000;
        double s =
            bench_one(fbblt_variant_scalar, n, srcX, dstX, alu, pm, reps);
#if __has_attribute(vector_size)
        double v =
            bench_one(fbblt_variant_vector, n, srcX, dstX, alu, pm, reps);
        double speed = (v > 0) ? s / v : 0;
        printf("%-8d %-14.1f %-14.1f %-10.2f\n", n, s, v, speed);
#else
        printf("%-8d %-14.1f\n", n, s);
#endif
    }
}

static int
verify_one(int nmiddle, int srcX, int dstX, int alu, FbBits pm)
{
    int width = nmiddle * FB_UNIT;
    // also test non-multiple width to hit startmask/endmask
    if (rand() % 3 == 0)
        width = (rand() % 1024) + 1;
    int height = 1 + rand() % 4;
    Bool reverse = rand() % 2;
    Bool upsidedown = rand() % 2;
    int srcStride = ((width + 31) / 32) + 4;
    int dstStride = srcStride;
    int sz = srcStride * height + 8;
    FbBits *src = calloc(sz, sizeof(FbBits));
    FbBits *dst1 = calloc(sz, sizeof(FbBits));
    FbBits *dst2 = calloc(sz, sizeof(FbBits));
    for (int i = 0; i < sz; i++) {
        src[i] = (FbBits)rand() ^ ((FbBits)rand() << 16);
        dst1[i] = dst2[i] = (FbBits)rand();
    }
    fbblt_variant_scalar(src, srcStride, srcX, dst1, dstStride, dstX, width,
                         height, alu, pm, reverse, upsidedown);
    fbblt_variant_vector(src, srcStride, srcX, dst2, dstStride, dstX, width,
                         height, alu, pm, reverse, upsidedown);
    int ok = (memcmp(dst1, dst2, sz * sizeof(FbBits)) == 0);
    free(src);
    free(dst1);
    free(dst2);
    return ok;
}

int
main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            g_height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--min-size") == 0 && i + 1 < argc) {
            g_min_size = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--height N] [--min-size N]\n",
                    argv[0]);
            return 1;
        }
    }
    if (g_height <= 0 || g_min_size <= 0) {
        fprintf(stderr, "height and min-size must be > 0\n");
        return 1;
    }
    printf("# fbBlt full scalar vs vector (height %d test)\n", g_height);

    // quick correctness check before bench
    int fails = 0;
    for (int i = 0; i < 2000; i++) {
        int n = 1 + rand() % 64;
        int srcX = rand() % 32, dstX = rand() % 32;
        int alus[] = {GXcopy, GXxor, GXand, GXor};
        int alu = alus[rand() % 4];
        FbBits pm = (rand() % 2) ? FB_ALLONES : (FbBits)rand();
        if (!verify_one(n, srcX, dstX, alu, pm))
            fails++;
    }
    printf(
        "# verify: 2000 random full fbBlt scalar vs vector %s (%d fails)\n",
        fails ? "FAIL" : "OK", fails);
    if (fails)
        return 1;

#if __has_attribute(vector_size)
    printf("# vector_size available, printing scalar vs vector\n");
#else
    printf("!!! vector_size not available, vector aliases scalar\n");
#endif

    print_table("aligned-copy  (srcX==dstX, GXcopy)", 0, 0, GXcopy,
                FB_ALLONES);
    print_table("aligned-xor   (srcX==dstX, GXxor)", 0, 0, GXxor, FB_ALLONES);
    print_table("unaligned-copy (srcX=7, GXcopy)", 7, 0, GXcopy, FB_ALLONES);
    print_table("unaligned-xor  (srcX=7, GXxor)", 7, 0, GXxor, FB_ALLONES);

    // extra table for general ROP with pm
    print_table("aligned-and  pm=0x55555555", 0, 0, GXand, 0x55555555);
    return 0;
}
