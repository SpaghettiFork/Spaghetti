/**
 * test/fbpolyseg-bench.c
 * Scalar vs vector for POLYSEGMENT hline fast path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "fbpolyseg-variants.h"

static double
bench_one(void (*fn)(FbBits *, int, FbBits, FbBits, FbBits, FbBits),
          int nmiddle, FbBits andBits, FbBits xorBits, long reps)
{
    int len = nmiddle + 2;
    FbBits *dst = calloc(len + 8, sizeof(FbBits));
    if (!dst) return 0;
    for (int i = 0; i < len + 8; i++)
        dst[i] = (FbBits)rand();
    FbBits startmask = 0, endmask = 0;
    if (rand() % 2) startmask = (FbBits)rand();
    if (rand() % 2) endmask = (FbBits)rand();
    for (int i = 0; i < 200; i++)
        fn(dst + 1, nmiddle, andBits, xorBits, startmask, endmask);
    struct timespec t0, t1;
    volatile FbBits sink = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < reps; i++)
        fn(dst + 1, nmiddle, andBits, xorBits, startmask, endmask);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < len; i++)
        sink ^= dst[i];
    (void)sink;
    free(dst);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (double)reps;
}

static void
print_table(const char *title, FbBits andBits, FbBits xorBits)
{
    static const int sizes[] = {4, 16, 64, 256, 1024, 4096};
    printf("\n# %s and=%08x xor=%08x\n", title, (unsigned)andBits, (unsigned)xorBits);
#if __has_attribute(vector_size)
    printf("%-8s %-14s %-14s %-10s\n", "nmiddle", "scalar(ns)", "vector(ns)", "speedup");
#else
    printf("%-8s %-14s\n", "nmiddle", "scalar(ns)");
#endif
    for (int i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
        int n = sizes[i];
        long reps = 5000000 / ((n ? n : 1));
        if (reps < 20) reps = 20;
        if (reps > 10000) reps = 10000;
        double s = bench_one(polyseg_hline_scalar, n, andBits, xorBits, reps);
#if __has_attribute(vector_size)
        double v = bench_one(polyseg_hline_vector, n, andBits, xorBits, reps);
        double speed = (v > 0) ? s / v : 0;
        printf("%-8d %-14.1f %-14.1f %-10.2f\n", n, s, v, speed);
#else
        printf("%-8d %-14.1f\n", n, s);
#endif
    }
}

static int
verify_one(int nmiddle, FbBits andBits, FbBits xorBits, FbBits sm, FbBits em)
{
    int len = nmiddle + 2;
    FbBits *a = calloc(len + 8, sizeof(FbBits));
    FbBits *b = calloc(len + 8, sizeof(FbBits));
    for (int i = 0; i < len + 8; i++) {
        FbBits r = (FbBits)rand();
        a[i] = b[i] = r;
    }
    polyseg_hline_scalar(a + 1, nmiddle, andBits, xorBits, sm, em);
    polyseg_hline_vector(b + 1, nmiddle, andBits, xorBits, sm, em);
    int ok = (memcmp(a, b, (len + 8) * sizeof(FbBits)) == 0);
    free(a); free(b);
    return ok;
}

int
main(void)
{
    printf("# polyseg hline scalar vs vector\n");
    int fails = 0;
    for (int i = 0; i < 2000; i++) {
        int n = rand() % 128;
        FbBits andBits = (rand() % 2) ? 0 : (FbBits)rand();
        FbBits xorBits = (FbBits)rand();
        FbBits sm = (rand() % 2) ? 0 : (FbBits)rand();
        FbBits em = (rand() % 2) ? 0 : (FbBits)rand();
        if (!verify_one(n, andBits, xorBits, sm, em))
            fails++;
    }
    printf("# verify: 2000 random polyseg hline %s (%d fails)\n", fails ? "FAIL" : "OK", fails);
    if (fails) return 1;
#if __has_attribute(vector_size)
    printf("# vector_size available\n");
#else
    printf("!!! vector_size not available\n");
#endif
    print_table("hline and==0", 0, 0x12345678);
    print_table("hline and!=0", 0x55555555, 0xAAAAAAAA);
    return 0;
}
