/**
 * test/fbtile-bench.c
 * 
 * Scalar vs vector benchmark for fbEvenTile middle loops.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "fbtile-variants.h"

#define GXcopy 0x3
#define GXxor 0x6

static int g_height = 64;
static int g_min_size = 4;

static double
bench_one(void (*fn)(FbBits *, FbStride, int, int, int, FbBits *, FbStride, int, int, FbBits, int, int),
          int nmiddle, int dstX, int alu, FbBits pm, int xRot, long reps)
{
    int width = nmiddle * FB_UNIT;
    int height = g_height;
    int dstStride = nmiddle + 8;
    int tileHeight = 8;
    int tileStride = 1;
    FbBits tile[32];
    FbBits *dst = calloc((size_t)dstStride * height + 8, sizeof(FbBits));
    for (int i = 0; i < 32; i++)
        tile[i] = (FbBits)rand() ^ ((FbBits)rand() << 16);
    if (!dst)
        return 0;
    for (int i = 0; i < dstStride * height + 8; i++)
        dst[i] = (FbBits)rand();
    for (int i = 0; i < 200; i++)
        fn(dst, dstStride, dstX, width, height, tile, tileStride, tileHeight, alu, pm, xRot, 0);
    struct timespec t0, t1;
    volatile FbBits sink = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < reps; i++)
        fn(dst, dstStride, dstX, width, height, tile, tileStride, tileHeight, alu, pm, xRot, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < dstStride * height + 8; i++)
        sink ^= dst[i];
    (void)sink;
    free(dst);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (double)reps;
}

static void
print_table(const char *title, int dstX, int alu, FbBits pm, int xRot)
{
    static const int sizes[] = {4, 16, 64, 256, 1024, 4096};
    printf("\n# %s (dstX=%d alu=%d pm=%08x xRot=%d height=%d)\n",
           title, dstX, alu, (unsigned)pm, xRot, g_height);
#if __has_attribute(vector_size)
    printf("%-8s %-14s %-14s %-10s\n", "nmiddle", "scalar(ns)", "vector(ns)", "speedup");
#else
    printf("%-8s %-14s\n", "nmiddle", "scalar(ns)");
#endif
    for (int i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
        int n = sizes[i];
        if (n < g_min_size) continue;
        int hf = g_height > 64 ? g_height/64 : 1;
        long reps = 200000 / ((n ? n : 1) * hf);
        if (reps < 20) reps = 20;
        if (reps > 5000) reps = 5000;
        double s = bench_one(fbtile_variant_scalar, n, dstX, alu, pm, xRot, reps);
#if __has_attribute(vector_size)
        double v = bench_one(fbtile_variant_vector, n, dstX, alu, pm, xRot, reps);
        double speed = (v > 0) ? s / v : 0;
        printf("%-8d %-14.1f %-14.1f %-10.2f\n", n, s, v, speed);
#else
        printf("%-8d %-14.1f\n", n, s);
#endif
    }
}

static int
verify_one(int nmiddle, int dstX, int alu, FbBits pm, int xRot, int yRot)
{
    int width = nmiddle * FB_UNIT;
    if (rand() % 3 == 0)
        width = (rand() % 2048) + 1;
    int height = 1 + rand() % 4;
    int dstStride = ((width + 31)/32) + 8;
    int tileHeight = 4 + rand() % 8;
    int tileStride = 1;
    FbBits tile[32];
    for (int i = 0; i < 32; i++)
        tile[i] = (FbBits)rand();
    dstX = rand() % 32;
    int sz = dstStride * height + 8;
    FbBits *d1 = calloc(sz, sizeof(FbBits));
    FbBits *d2 = calloc(sz, sizeof(FbBits));
    for (int i = 0; i < sz; i++) {
        FbBits r = (FbBits)rand();
        d1[i] = d2[i] = r;
    }
    fbtile_variant_scalar(d1, dstStride, dstX, width, height, tile, tileStride, tileHeight, alu, pm, xRot, yRot);
    fbtile_variant_vector(d2, dstStride, dstX, width, height, tile, tileStride, tileHeight, alu, pm, xRot, yRot);
    int ok = (memcmp(d1, d2, sz * sizeof(FbBits)) == 0);
    free(d1); free(d2);
    return ok;
}

int
main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) g_height = atoi(argv[++i]);
        else if (strcmp(argv[i], "--min-size") == 0 && i + 1 < argc) g_min_size = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [--height N] [--min-size N]\n", argv[0]); return 1; }
    }
    printf("# fbtile (even) scalar vs vector (height %d)\n", g_height);
    int fails = 0;
    for (int i = 0; i < 2000; i++) {
        int n = 1 + rand() % 64;
        int alu = (rand() % 2) ? GXcopy : GXxor;
        FbBits pm = (rand() % 2) ? FB_ALLONES : (FbBits)rand();
        int xRot = rand() % 32;
        int yRot = rand() % 8;
        if (!verify_one(n, rand() % 32, alu, pm, xRot, yRot))
            fails++;
    }
    printf("# verify: 2000 random fbtile scalar vs vector %s (%d fails)\n", fails ? "FAIL" : "OK", fails);
    if (fails) return 1;
#if __has_attribute(vector_size)
    printf("# vector_size available\n");
#else
    printf("!!! vector_size not available\n");
#endif
    print_table("tile GXcopy pm=ALLONES xRot=0", 0, GXcopy, FB_ALLONES, 0);
    print_table("tile GXcopy pm=ALLONES xRot=7", 0, GXcopy, FB_ALLONES, 7);
    print_table("tile GXxor pm=ALLONES", 0, GXxor, FB_ALLONES, 3);
    print_table("tile dstX=7 GXcopy", 7, GXcopy, FB_ALLONES, 0);
    return 0;
}
