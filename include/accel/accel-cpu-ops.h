/*
 * Accelerator per-vCPU handlers
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_ACCEL_CPU_OPS_H
#define QEMU_ACCEL_CPU_OPS_H

#include "qemu/accel.h"
#include "exec/vaddr.h"
#include "qom/object.h"

#define ACCEL_OPS_SUFFIX "-ops"
#define TYPE_ACCEL_OPS "accel" ACCEL_OPS_SUFFIX
#define ACCEL_OPS_NAME(name) (name "-" TYPE_ACCEL_OPS)

DECLARE_CLASS_CHECKERS(AccelOpsClass, ACCEL_OPS, TYPE_ACCEL_OPS)

struct AccelOpsClass {
    ObjectClass parent_class;

    void (*ops_init)(AccelClass *ac);

    bool (*cpus_are_resettable)(void);
    void (*cpu_reset_hold)(CPUState *cpu);

    void (*create_vcpu_thread)(CPUState *cpu);
    void (*kick_vcpu_thread)(CPUState *cpu);
    bool (*cpu_thread_is_idle)(CPUState *cpu);

    void (*synchronize_post_reset)(CPUState *cpu);
    void (*synchronize_post_init)(CPUState *cpu);
    void (*synchronize_state)(CPUState *cpu);
    void (*synchronize_pre_loadvm)(CPUState *cpu);

    void (*handle_interrupt)(CPUState *cpu, int mask);

    void (*get_vcpu_stats)(CPUState *cpu, GString *buf);

    int64_t (*get_virtual_clock)(void);
    void (*set_virtual_clock)(int64_t time);

    int64_t (*get_elapsed_ticks)(void);

    bool (*supports_guest_debug)(void);
    int (*update_guest_debug)(CPUState *cpu);
    int (*insert_breakpoint)(CPUState *cpu, int type, vaddr addr, vaddr len);
    int (*remove_breakpoint)(CPUState *cpu, int type, vaddr addr, vaddr len);
    void (*remove_all_breakpoints)(CPUState *cpu);
};

void generic_handle_interrupt(CPUState *cpu, int mask);

#endif
