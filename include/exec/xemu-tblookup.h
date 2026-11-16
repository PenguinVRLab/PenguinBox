/*
 * Copyright (c) 2026 Patrick Carey
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EXEC_XEMU_TBLOOKUP_H
#define EXEC_XEMU_TBLOOKUP_H

#include "hw/core/cpu.h"
#include "qemu/log.h"

extern int xemu_tb_fastptr;


static inline void xemu_fastptr_gate_update(CPUState *cpu, uint32_t cflags_now)
{
    if (likely(!xemu_tb_fastptr)) {
        return;
    }
    bool on = cflags_now == cpu->tcg_cflags
        && !qemu_loglevel_mask(CPU_LOG_TB_CPU | CPU_LOG_EXEC);
    bool no_bp = QTAILQ_EMPTY(&cpu->breakpoints);
    cpu->xemu_fastptr_gate = on && no_bp;
}

static inline void xemu_fastptr_gate_close_all(void)
{
    CPUState *cpu;

    if (likely(!xemu_tb_fastptr)) {
        return;
    }
    CPU_FOREACH(cpu) {
        qatomic_set(&cpu->xemu_fastptr_gate, 0);
        cpu_exit(cpu);
    }
}

static inline void xemu_fastptr_breakpoint_inserted(CPUState *cpu)
{
    cpu->xemu_fastptr_gate = 0;
}

#endif
