/*
 * QEMU emulation of an RISC-V IOMMU
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_IOMMU_STATE_H
#define HW_RISCV_IOMMU_STATE_H

#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "system/dma.h"
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu-bits.h"

typedef enum riscv_iommu_igs_modes riscv_iommu_igs_mode;

struct RISCVIOMMUState {
    DeviceState parent_obj;

    uint32_t version;
    uint32_t pid_bits;
    uint32_t bus;

    uint64_t cap;
    uint64_t fctl;
    uint64_t icvec_avail_vectors;

    bool enable_off;
    bool enable_msi;
    bool enable_ats;
    bool enable_s_stage;
    bool enable_g_stage;

    uint64_t ddtp;

    dma_addr_t cq_addr;
    dma_addr_t fq_addr;
    dma_addr_t pq_addr;

    uint32_t cq_mask;
    uint32_t fq_mask;
    uint32_t pq_mask;

    void (*notify)(RISCVIOMMUState *iommu, unsigned vector);

    AddressSpace *target_as;
    MemoryRegion *target_mr;

    AddressSpace trap_as;
    MemoryRegion trap_mr;

    GHashTable *ctx_cache;

    GHashTable *iot_cache;
    unsigned iot_limit;

    MemoryRegion regs_mr;
    uint8_t *regs_rw;
    uint8_t *regs_wc;
    uint8_t *regs_ro;

    QLIST_ENTRY(RISCVIOMMUState) iommus;
    QLIST_HEAD(, RISCVIOMMUSpace) spaces;

    QEMUTimer *hpm_timer;
    uint64_t hpmcycle_val;
    uint64_t hpmcycle_prev;
    uint64_t irq_overflow_left;

    GHashTable *hpm_event_ctr_map;
    uint8_t hpm_cntrs;
};

void riscv_iommu_pci_setup_iommu(RISCVIOMMUState *iommu, PCIBus *bus,
         Error **errp);
void riscv_iommu_set_cap_igs(RISCVIOMMUState *s, riscv_iommu_igs_mode mode);
void riscv_iommu_reset(RISCVIOMMUState *s);
void riscv_iommu_notify(RISCVIOMMUState *s, int vec_type);

typedef struct RISCVIOMMUContext RISCVIOMMUContext;
struct RISCVIOMMUContext {
    uint64_t devid:24;
    uint64_t process_id:20;
    uint64_t tc;
    uint64_t ta;
    uint64_t satp;
    uint64_t gatp;
    uint64_t msi_addr_mask;
    uint64_t msi_addr_pattern;
    uint64_t msiptp;
};

static inline uint32_t riscv_iommu_reg_mod32(RISCVIOMMUState *s,
    unsigned idx, uint32_t set, uint32_t clr)
{
    uint32_t val = ldl_le_p(s->regs_rw + idx);
    stl_le_p(s->regs_rw + idx, (val & ~clr) | set);
    return val;
}

static inline void riscv_iommu_reg_set32(RISCVIOMMUState *s, unsigned idx,
                                         uint32_t set)
{
    stl_le_p(s->regs_rw + idx, set);
}

static inline uint32_t riscv_iommu_reg_get32(RISCVIOMMUState *s, unsigned idx)
{
    return ldl_le_p(s->regs_rw + idx);
}

static inline uint64_t riscv_iommu_reg_mod64(RISCVIOMMUState *s, unsigned idx,
                                             uint64_t set, uint64_t clr)
{
    uint64_t val = ldq_le_p(s->regs_rw + idx);
    stq_le_p(s->regs_rw + idx, (val & ~clr) | set);
    return val;
}

static inline void riscv_iommu_reg_set64(RISCVIOMMUState *s, unsigned idx,
                                         uint64_t set)
{
    stq_le_p(s->regs_rw + idx, set);
}

static inline uint64_t riscv_iommu_reg_get64(RISCVIOMMUState *s,
    unsigned idx)
{
    return ldq_le_p(s->regs_rw + idx);
}
#endif
