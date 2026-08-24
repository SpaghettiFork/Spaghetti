/**
 * test/fbimage-bench.c
 * 
 * Scalar vs vector for fbGetImage pm &= loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "fbimage-variants.h"

static double
bench_one(void (*fn)(FbStip *, int, FbBits), int len, FbBits pm, long reps)
{
    FbStip *dst = calloc(len + 8, sizeof(FbStip));
    if (!dst) return 0;
    for (int i = 0; i < len + 8; i++)
        dst[i] = (FbStip)rand() ^ ((FbStip)rand() << 16);
    for (int i = 0; i < 200; i++)
        fn(dst, len, pm);
    struct timespec t0, t1;
    volatile FbStip sink = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < reps; i++)
        fn(dst, len, pm);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < len; i++)
        sink ^= dst[i];
    (void)sink;
    free(dst);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (double)reps;
}

static void
print_table(const char *title, FbBits pm)
{
    static const int sizes[] = {64, 256, 1024, 4096, 16384, 65536, 262144};
    printf("\n# %s pm=%08x\n", title, (unsigned)pm);
#if __has_attribute(vector_size)
    printf("%-10s %-14s %-14s %-10s\n", "len", "scalar(ns)", "vector(ns)", "speedup");
#else
    printf("%-10s %-14s\n", "len", "scalar(ns)");
#endif
    for (int i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
        int n = sizes[i];
        long reps = 5000000 / (n ? n : 1);
        if (reps < 20) reps = 20;
        if (reps > 10000) reps = 10000;
        double s = bench_one(fbimage_pm_scalar, n, pm, reps);
#if __has_attribute(vector_size)
        double v = bench_one(fbimage_pm_vector, n, pm, reps);
        double speed = (v > 0) ? s / v : 0;
        printf("%-10d %-14.1f %-14.1f %-10.2f\n", n, s, v, speed);
#else
        printf("%-10d %-14.1f\n", n, s);
#endif
    }
}

static int
verify_one(int len, FbBits pm)
{
    FbStip *a = calloc(len + 4, sizeof(FbStip));
    FbStip *b = calloc(len + 4, sizeof(FbStip));
    for (int i = 0; i < len + 4; i++) {
        FbStip r = (FbStip)rand();
        a[i] = b[i] = r;
    }
    fbimage_pm_scalar(a, len, pm);
    fbimage_pm_vector(b, len, pm);
    int ok = (memcmp(a, b, len * sizeof(FbStip)) == 0);
    free(a); free(b);
    return ok;
}

int
main(void)
{
    printf("# fbimage pm scalar vs vector\n");
    int fails = 0;
    for (int i = 0; i < 2000; i++) {
        int len = 1 + rand() % 4096;
        FbBits pm = (rand() % 4 == 0) ? (FbBits)-1 : (FbBits)rand();
        if (!verify_one(len, pm))
            fails++;
    }
    printf("# verify: 2000 random fbimage pm %s (%d fails)\n", fails ? "FAIL" : "OK", fails);
    if (fails) return 1;
#if __has_attribute(vector_size)
    printf("# vector_size available\n");
#else
    printf("!!! vector_size not available\n");
#endif
    print_table("pm 0x55555555", 0x55555555);
    print_table("pm 0x0F0F0F0F", 0x0F0F0F0F);
    print_table("pm ALLONES (no-op)", (FbBits)-1);
    return 0;
}
