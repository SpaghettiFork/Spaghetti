/**
 * test/fbsolid-bench.c
 *
 * Scalar vs vector benchmark for fbSolid middle loops.
 * Mirrors test/fbblt-bench.c and test/region-bench.c style.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "fbsolid-variants.h"

static int g_height = 64;
static int g_min_size = 4;

static double
bench_one(void (*fn)(FbBits *, FbStride, int, int, int, FbBits, FbBits),
          int nmiddle, int dstX, FbBits and, FbBits xor, long reps)
{
    int width = nmiddle * FB_UNIT;
    int height = g_height;
    int stride = nmiddle + 8;
    FbBits *dst = calloc((size_t)stride * height + 8, sizeof(FbBits));
    struct timespec t0, t1;
    long i;
    volatile FbBits sink = 0;

    if (!dst)
        return 0;
    for (i = 0; i < (long)stride * height + 8; i++)
        dst[i] = (FbBits)rand() ^ ((FbBits)rand() << 16);

    for (i = 0; i < 200; i++)
        fn(dst, stride, dstX, width, height, and, xor);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < reps; i++)
        fn(dst, stride, dstX, width, height, and, xor);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    for (i = 0; i < (long)stride * height + 8; i++)
        sink ^= dst[i];
    (void)sink;
    free(dst);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (double)reps;
}

static void
print_table(const char *title, int dstX, FbBits and, FbBits xor)
{
    static const int sizes[] = {4, 16, 64, 256, 1024, 4096};
    int i;
    printf("\n# %s (dstX=%d and=%08x xor=%08x height=%d width=nmiddle*%d)\n",
           title, dstX, (unsigned)and, (unsigned)xor, g_height, FB_UNIT);
#if __has_attribute(vector_size)
    printf("%-8s %-14s %-14s %-10s\n", "nmiddle", "scalar(ns)", "vector(ns)", "speedup");
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
        double s = bench_one(fbsolid_variant_scalar, n, dstX, and, xor, reps);
#if __has_attribute(vector_size)
        double v = bench_one(fbsolid_variant_vector, n, dstX, and, xor, reps);
        double speed = (v > 0) ? s / v : 0;
        printf("%-8d %-14.1f %-14.1f %-10.2f\n", n, s, v, speed);
#else
        printf("%-8d %-14.1f\n", n, s);
#endif
    }
}

static int
verify_one(int nmiddle, int dstX, FbBits and, FbBits xor)
{
    int width = nmiddle * FB_UNIT;
    if (rand() % 3 == 0)
        width = (rand() % 2048) + 1;
    int height = 1 + rand() % 4;
    int stride = ((width + 31) / 32) + 8;
    dstX = rand() % 32;
    int sz = stride * height + 8;
    FbBits *dst1 = calloc(sz, sizeof(FbBits));
    FbBits *dst2 = calloc(sz, sizeof(FbBits));
    for (int i = 0; i < sz; i++) {
        FbBits r = (FbBits)rand() ^ ((FbBits)rand() << 16);
        dst1[i] = dst2[i] = r;
    }
    fbsolid_variant_scalar(dst1, stride, dstX, width, height, and, xor);
    fbsolid_variant_vector(dst2, stride, dstX, width, height, and, xor);
    int ok = (memcmp(dst1, dst2, sz * sizeof(FbBits)) == 0);
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
            fprintf(stderr, "usage: %s [--height N] [--min-size N]\n", argv[0]);
            return 1;
        }
    }
    if (g_height <= 0 || g_min_size <= 0) {
        fprintf(stderr, "height and min-size must be > 0\n");
        return 1;
    }
    printf("# fbsolid scalar vs vector (height %d)\n", g_height);
    int fails = 0;
    for (int i = 0; i < 2000; i++) {
        int n = 1 + rand() % 64;
        FbBits and = (rand() % 2) ? 0 : (FbBits)rand();
        FbBits xor = (FbBits)rand();
        int dstX = rand() % 32;
        if (!verify_one(n, dstX, and, xor))
            fails++;
    }
    printf("# verify: 2000 random fbsolid scalar vs vector %s (%d fails)\n",
           fails ? "FAIL" : "OK", fails);
    if (fails)
        return 1;

#if __has_attribute(vector_size)
    printf("# vector_size available, printing scalar vs vector\n");
#else
    printf("!!! vector_size not available, vector aliases scalar\n");
#endif

    print_table("solid-fill and==0 (GXcopy)", 0, 0, 0x12345678);
    print_table("solid-fill and!=0 (GXxor)", 0, 0x55555555, 0x12345678);
    print_table("solid-fill dstX=7 and==0", 7, 0, 0xAAAAAAAA);
    print_table("solid-fill dstX=7 and!=0", 7, 0x0F0F0F0F, 0xF0F0F0F0);
    return 0;
}
