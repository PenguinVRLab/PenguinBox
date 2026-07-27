/*
 * QEMU PowerPC PowerNV CPU Core model
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_CORE_H
#define PPC_PNV_CORE_H

#include "hw/cpu/core.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/pnv.h"
#include "qom/object.h"

typedef struct PnvCoreTODState {
    bool big_core_quirk;

    int tb_ready_for_tod;
    int tod_sent_to_tb;

    int tb_state_timer;
    int tb_sync_pulse_timer;
} PnvCoreTODState;

#define TYPE_PNV_CORE "powernv-cpu-core"
OBJECT_DECLARE_TYPE(PnvCore, PnvCoreClass,
                    PNV_CORE)

struct PnvCore {
    CPUCore parent_obj;

    PowerPCCPU **threads;
    bool big_core;
    bool lpar_per_core;
    uint32_t pir;
    uint32_t hwid;
    uint64_t hrmor;

    target_ulong scratch[8];
    PnvCoreTODState tod_state;

    PnvChip *chip;

    MemoryRegion xscom_regs;
};

struct PnvCoreClass {
    DeviceClass parent_class;

    const MemoryRegionOps *xscom_ops;
    uint64_t xscom_size;
};

#define PNV_CORE_TYPE_SUFFIX "-" TYPE_PNV_CORE
#define PNV_CORE_TYPE_NAME(cpu_model) cpu_model PNV_CORE_TYPE_SUFFIX

typedef struct PnvCPUState {
    PnvCore *pnv_core;
    Object *intc;
} PnvCPUState;

static inline PnvCPUState *pnv_cpu_state(PowerPCCPU *cpu)
{
    return (PnvCPUState *)cpu->machine_data;
}

struct PnvQuadClass {
    DeviceClass parent_class;

    const MemoryRegionOps *xscom_ops;
    uint64_t xscom_size;

    const MemoryRegionOps *xscom_qme_ops;
    uint64_t xscom_qme_size;
};

#define TYPE_PNV_QUAD "powernv-cpu-quad"

#define PNV_QUAD_TYPE_SUFFIX "-" TYPE_PNV_QUAD
#define PNV_QUAD_TYPE_NAME(cpu_model) cpu_model PNV_QUAD_TYPE_SUFFIX

OBJECT_DECLARE_TYPE(PnvQuad, PnvQuadClass, PNV_QUAD)

struct PnvQuad {
    DeviceState parent_obj;

    bool special_wakeup_done;
    bool special_wakeup[4];

    uint32_t quad_id;
    MemoryRegion xscom_regs;
    MemoryRegion xscom_qme_regs;
};
#endif
