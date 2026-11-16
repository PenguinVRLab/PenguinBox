/*
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef TARGET_I386_XEMU_TB_KEY_H
#define TARGET_I386_XEMU_TB_KEY_H

#include "cpu.h"
#include "accel/tcg/tb-cpu-state.h"

#define X86_TB_KEY_EFLAGS_MASK (IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK)

static inline __attribute__((always_inline)) void x86_tb_key(CPUX86State *env, TCGTBCPUState *s)
{
    s->flags = env->hflags | (env->eflags & X86_TB_KEY_EFLAGS_MASK);
    if (env->hflags & HF_CS64_MASK) {
        s->cs_base = 0;
        s->pc = env->eip;
    } else {
        s->cs_base = env->segs[R_CS].base;
        s->pc = (uint32_t)(s->cs_base + env->eip);
    }
}

#endif
