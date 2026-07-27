/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic extract 64 from 128-bit, LoongArch version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef LOONGARCH_LOAD_EXTRACT_AL16_AL8_H
#define LOONGARCH_LOAD_EXTRACT_AL16_AL8_H

#include "host/cpuinfo.h"
#include "tcg/debug-assert.h"

static inline uint64_t load_atom_extract_al16_or_al8(void *pv, int s)
{
    uintptr_t pi = (uintptr_t)pv;
    Int128 *ptr_align = (Int128 *)(pi & ~7);
    int shr = (pi & 7) * 8;
    uint64_t l, h;

    tcg_debug_assert(HAVE_ATOMIC128_RO);
    asm("vld $vr0, %2, 0\n\t"
        "vpickve2gr.d %0, $vr0, 0\n\t"
        "vpickve2gr.d %1, $vr0, 1"
        : "=r"(l), "=r"(h) : "r"(ptr_align), "m"(*ptr_align) : "$f0");

    return (l >> shr) | (h << (-shr & 63));
}

#endif
