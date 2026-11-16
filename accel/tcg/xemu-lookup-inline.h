/*
 * Copyright (c) 2003-2005 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef ACCEL_TCG_XEMU_LOOKUP_INLINE_H
#define ACCEL_TCG_XEMU_LOOKUP_INLINE_H

#include "hw/core/cpu.h"
#include "exec/translation-block.h"
#include "qemu/log.h"
#include "internal-common.h"

static inline __attribute__((always_inline)) uint32_t curr_cflags_inline(CPUState *cpu)
{
    uint32_t cflags = cpu->tcg_cflags;

    if (unlikely(cpu->singlestep_enabled)) {
        cflags |= CF_NO_GOTO_TB | CF_NO_GOTO_PTR | CF_SINGLE_STEP | 1;
    } else if (unlikely(qatomic_read(&one_insn_per_tb))) {
        cflags |= CF_NO_GOTO_TB | 1;
    } else if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN))) {
        cflags |= CF_NO_GOTO_TB;
    }


    return cflags;
}

#endif
