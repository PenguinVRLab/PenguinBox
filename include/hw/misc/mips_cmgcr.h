/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Imagination Technologies
 *
 */

#ifndef MIPS_CMGCR_H
#define MIPS_CMGCR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MIPS_GCR "mips-gcr"
OBJECT_DECLARE_SIMPLE_TYPE(MIPSGCRState, MIPS_GCR)

#define GCR_BASE_ADDR           0x1fbf8000ULL
#define GCR_ADDRSPACE_SZ        0x8000

#define MIPS_GCB_OFS        0x0000
#define MIPS_CLCB_OFS       0x2000
#define MIPS_COCB_OFS       0x4000
#define MIPS_GDB_OFS        0x6000

#define GCR_CONFIG_OFS      0x0000
#define GCR_BASE_OFS        0x0008
#define GCR_REV_OFS         0x0030
#define GCR_GIC_BASE_OFS    0x0080
#define GCR_CPC_BASE_OFS    0x0088
#define GCR_GIC_STATUS_OFS  0x00D0
#define GCR_CPC_STATUS_OFS  0x00F0
#define GCR_L2_CONFIG_OFS   0x0130

#define GCR_CL_CONFIG_OFS   0x0010
#define GCR_CL_OTHER_OFS    0x0018
#define GCR_CL_RESETBASE_OFS 0x0020

#define GCR_L2_CONFIG_BYPASS_SHF    20
#define GCR_L2_CONFIG_BYPASS_MSK    ((0x1ULL) << GCR_L2_CONFIG_BYPASS_SHF)

#define GCR_BASE_GCRBASE_MSK     0xffffffff8000ULL

#define GCR_GIC_BASE_GICEN_MSK   1
#define GCR_GIC_BASE_GICBASE_MSK 0xFFFFFFFE0000ULL
#define GCR_GIC_BASE_MSK (GCR_GIC_BASE_GICEN_MSK | GCR_GIC_BASE_GICBASE_MSK)

#define GCR_CPC_BASE_CPCEN_MSK   1
#define GCR_CPC_BASE_CPCBASE_MSK 0xFFFFFFFF8000ULL
#define GCR_CPC_BASE_MSK (GCR_CPC_BASE_CPCEN_MSK | GCR_CPC_BASE_CPCBASE_MSK)

#define GCR_CL_OTHER_VPOTHER_MSK 0x7
#define GCR_CL_OTHER_MSK GCR_CL_OTHER_VPOTHER_MSK

#define GCR_CL_RESET_BASE_RESETBASE_MSK 0xFFFFF000U
#define GCR_CL_RESET_BASE_MSK GCR_CL_RESET_BASE_RESETBASE_MSK

typedef struct MIPSGCRVPState MIPSGCRVPState;
struct MIPSGCRVPState {
    uint32_t other;
    uint64_t reset_base;
};

struct MIPSGCRState {
    SysBusDevice parent_obj;

    int32_t gcr_rev;
    uint32_t num_vps;
    hwaddr gcr_base;
    MemoryRegion iomem;
    MemoryRegion *cpc_mr;
    MemoryRegion *gic_mr;

    uint64_t cpc_base;
    uint64_t gic_base;

    MIPSGCRVPState *vps;
};

#endif
