/*
 * Accelerator handlers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_OPS_H
#define ACCEL_OPS_H

#include "exec/hwaddr.h"
#include "qemu/accel.h"
#include "qom/object.h"

struct AccelState {
    Object parent_obj;
};

struct AccelClass {
    ObjectClass parent_class;

    const char *name;
    AccelOpsClass *ops;

    int (*init_machine)(AccelState *as, MachineState *ms);
    bool (*cpu_common_realize)(CPUState *cpu, Error **errp);
    void (*cpu_common_unrealize)(CPUState *cpu);
    void (*get_stats)(AccelState *as, GString *buf);

    void (*setup_post)(AccelState *as);
    void (*pre_resume_vm)(AccelState *as, bool step_pending);
    bool (*has_memory)(AccelState *accel, AddressSpace *as,
                       hwaddr start_addr, hwaddr size);

    int (*gdbstub_supported_sstep_flags)(AccelState *as);

    bool *allowed;
    GPtrArray *compat_props;
};

#endif
