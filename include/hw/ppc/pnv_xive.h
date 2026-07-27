/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_XIVE_H
#define PPC_PNV_XIVE_H

#include "hw/ppc/pnv.h"
#include "hw/ppc/xive.h"
#include "qom/object.h"
#include "hw/ppc/xive2.h"

#define TYPE_PNV_XIVE "pnv-xive"
OBJECT_DECLARE_TYPE(PnvXive, PnvXiveClass,
                    PNV_XIVE)

#define XIVE_BLOCK_MAX      16

#define XIVE_TABLE_BLK_MAX  16
#define XIVE_TABLE_MIG_MAX  16
#define XIVE_TABLE_VDT_MAX  16
#define XIVE_TABLE_EDT_MAX  64

struct PnvXive {
    XiveRouter    parent_obj;

    PnvChip *chip;

    MemoryRegion  xscom_regs;

    MemoryRegion  ic_mmio;
    MemoryRegion    ic_reg_mmio;
    MemoryRegion    ic_notify_mmio;
    MemoryRegion    ic_lsi_mmio;
    MemoryRegion    tm_indirect_mmio;
    MemoryRegion  vc_mmio;
    MemoryRegion  pc_mmio;
    MemoryRegion  tm_mmio;

    AddressSpace  ipi_as;
    MemoryRegion  ipi_mmio;
    MemoryRegion    ipi_edt_mmio;

    AddressSpace  end_as;
    MemoryRegion  end_mmio;
    MemoryRegion    end_edt_mmio;

    hwaddr        ic_base;
    uint32_t      ic_shift;
    hwaddr        vc_base;
    uint32_t      vc_shift;
    hwaddr        pc_base;
    uint32_t      pc_shift;
    hwaddr        tm_base;
    uint32_t      tm_shift;

    XiveSource    ipi_source;
    XiveENDSource end_source;

    uint64_t      regs[0x300];

    uint64_t      vsds[5][XIVE_BLOCK_MAX];

    uint64_t      blk[XIVE_TABLE_BLK_MAX];
    uint64_t      mig[XIVE_TABLE_MIG_MAX];
    uint64_t      vdt[XIVE_TABLE_VDT_MAX];
    uint64_t      edt[XIVE_TABLE_EDT_MAX];
};

struct PnvXiveClass {
    XiveRouterClass parent_class;

    DeviceRealize parent_realize;
};

void pnv_xive_pic_print_info(PnvXive *xive, GString *buf);

#define TYPE_PNV_XIVE2 "pnv-xive2"
OBJECT_DECLARE_TYPE(PnvXive2, PnvXive2Class, PNV_XIVE2);

typedef struct PnvXive2 {
    Xive2Router   parent_obj;

    PnvChip *chip;

    MemoryRegion  xscom_regs;

    MemoryRegion  ic_mmio;
    MemoryRegion  ic_mmios[8];
    MemoryRegion  esb_mmio;
    MemoryRegion  end_mmio;
    MemoryRegion  nvc_mmio;
    MemoryRegion  nvpg_mmio;
    MemoryRegion  tm_mmio;

    hwaddr        ic_base;
    uint32_t      ic_shift;
    hwaddr        esb_base;
    uint32_t      esb_shift;
    hwaddr        end_base;
    uint32_t      end_shift;
    hwaddr        nvc_base;
    uint32_t      nvc_shift;
    hwaddr        nvpg_base;
    uint32_t      nvpg_shift;
    hwaddr        tm_base;
    uint32_t      tm_shift;

    uint64_t      cq_regs[0x40];
    uint64_t      vc_regs[0x100];
    uint64_t      pc_regs[0x100];
    uint64_t      tctxt_regs[0x30];

    uint64_t      capabilities;
    uint64_t      config;

    XiveSource    ipi_source;
    Xive2EndSource end_source;

    uint64_t      vsds[9][XIVE_BLOCK_MAX];

    uint64_t      tables[8][XIVE_BLOCK_MAX];

} PnvXive2;

typedef struct PnvXive2Class {
    Xive2RouterClass parent_class;

    DeviceRealize parent_realize;
} PnvXive2Class;

void pnv_xive2_pic_print_info(PnvXive2 *xive, GString *buf);

#endif
