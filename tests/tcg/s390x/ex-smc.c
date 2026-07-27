/*
 * Test modifying an EXECUTE target.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

extern void execute(unsigned char *insn, unsigned char mask,
                    unsigned long *r1_r5);
asm(".globl execute\n"
    "execute:\n"
    "lg %r1,0(%r4)\n"
    "lg %r5,8(%r4)\n"
    "ex %r3,0(%r2)\n"
    "stg %r5,8(%r4)\n"
    "stg %r1,0(%r4)\n"
    "br %r14\n");

extern unsigned char lgfi[];
asm(".pushsection .rwx,\"awx\",@progbits\n"
    ".globl lgfi\n"
    "lgfi: lgfi %r0,0\n"
    ".popsection\n");

int main(void)
{
    unsigned long r1_r5[2];

    r1_r5[0] = -1;
    r1_r5[1] = -1;
    execute(lgfi, 1 << 4, r1_r5);
    assert(r1_r5[0] == 0);
    assert(r1_r5[1] == -1);

    execute(lgfi, 5 << 4, r1_r5);
    assert(r1_r5[0] == 0);
    assert(r1_r5[1] == 0);

    lgfi[5] = 42;
    execute(lgfi, 5 << 4, r1_r5);
    assert(r1_r5[0] == 0);
    assert(r1_r5[1] == 42);

    lgfi[5] = 24;
    execute(lgfi, 1 << 4, r1_r5);
    assert(r1_r5[0] == 24);
    assert(r1_r5[1] == 42);

    return EXIT_SUCCESS;
}
