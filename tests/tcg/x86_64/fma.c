/*
 * Test some fused multiply add corner cases.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static uint64_t do_fmadd(uint64_t n, uint64_t m, uint64_t a,
                         bool ftz, uint32_t *mxcsr_p)
{
    uint64_t r;
    uint32_t mxcsr = 0;
    uint32_t ftz_bit = ftz ? (1 << 15) : 0;
    uint32_t saved_mxcsr = 0;

    asm volatile("stmxcsr %[saved_mxcsr]\n"
                 "stmxcsr %[mxcsr]\n"
                 "andl $0xffff7fc0, %[mxcsr]\n"
                 "orl %[ftz_bit], %[mxcsr]\n"
                 "ldmxcsr %[mxcsr]\n"
                 "movq %[a], %%xmm0\n"
                 "movq %[m], %%xmm1\n"
                 "movq %[n], %%xmm2\n"
                 "vfmadd231sd %%xmm1, %%xmm2, %%xmm0\n"
                 "movq %%xmm0, %[r]\n"
                 "stmxcsr %[mxcsr]\n"
                 "ldmxcsr %[saved_mxcsr]\n"
                 : [r] "=r" (r), [mxcsr] "=m" (mxcsr),
                   [saved_mxcsr] "=m" (saved_mxcsr)
                 : [n] "r" (n), [m] "r" (m), [a] "r" (a),
                   [ftz_bit] "r" (ftz_bit)
                 : "xmm0", "xmm1", "xmm2");
    *mxcsr_p = mxcsr & 0x3f;
    printf("vfmadd132sd 0x%" PRIx64 " 0x%" PRIx64 " 0x%" PRIx64
           " = 0x%" PRIx64 " MXCSR flags 0x%" PRIx32 "\n",
           n, m, a, r, *mxcsr_p);
    return r;
}

typedef struct testdata {
    uint64_t n;
    uint64_t m;
    uint64_t a;
    bool ftz;
    uint64_t expected_r;
    uint32_t expected_mxcsr;
} testdata;

static testdata tests[] = {
    { 0, 0x7ff0000000000000, 0x7ff000000000aaaa, false,
      0x7ff800000000aaaa, 1 },
    { 0, 0x7ff0000000000000, 0x7ff800000000aaaa, false,
      0x7ff800000000aaaa, 0 },
    { 0x3fdfffffffffffff, 0x001fffffffffffff, 0x801fffffffffffff, false,
      0x8010000000000000, 0x20 },
    { 0x3fdfffffffffffff, 0x001fffffffffffff, 0x801fffffffffffff, true,
      0x8010000000000000, 0x20 },
    { 0x3cc8000000000000, 0x0000000000000000, 0x8008000000000000, false,
      0x8008000000000000, 0x2 },
    { 0x3cc8000000000000, 0x0000000000000000, 0x8008000000000000, true,
      0x8000000000000000, 0x32 },
};

int main(void)
{
    bool passed = true;
    for (int i = 0; i < ARRAY_SIZE(tests); i++) {
        uint32_t mxcsr;
        uint64_t r = do_fmadd(tests[i].n, tests[i].m, tests[i].a,
                              tests[i].ftz, &mxcsr);
        if (r != tests[i].expected_r) {
            printf("expected result 0x%" PRIx64 "\n", tests[i].expected_r);
            passed = false;
        }
        if (mxcsr != tests[i].expected_mxcsr) {
            printf("expected MXCSR flags 0x%x\n", tests[i].expected_mxcsr);
            passed = false;
        }
    }
    return passed ? 0 : 1;
}
