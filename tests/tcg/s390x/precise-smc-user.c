/*
 * Test s390x-linux-user precise self-modifying code handling.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>

extern __uint128_t __attribute__((__aligned__(1))) smc;
extern __uint128_t __attribute__((__aligned__(1))) patch;

int main(void)
{
    char *aligned_smc = (char *)((uintptr_t)&smc & ~0xFFFULL);
    char *smc_end = (char *)&smc + sizeof(smc);
    uint64_t value = 21;
    int err;

    err = mprotect(aligned_smc, smc_end - aligned_smc,
                   PROT_READ | PROT_WRITE | PROT_EXEC);
    assert(err == 0);

    asm("jg 0f\n"
        "patch: .byte 0,0,0,0,0,0\n"
        ".byte 0,0,0,0,0,0\n"
        "agr %[value],%[value]\n"
        "smc: .org . + 6\n"
        "0: vstl %[patch],%[idx],%[smc]\n"
        "sgr %[value],%[value]"
        : [smc] "=R" (smc)
        , [value] "+r" (value)
        : [patch] "v" (patch)
        , [idx] "r" (sizeof(patch) - 1)
        : "cc");

    return value == 42 ? EXIT_SUCCESS : EXIT_FAILURE;
}
