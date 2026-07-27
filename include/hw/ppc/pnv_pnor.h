/*
 * QEMU PowerNV PNOR simple model
 *
 * Copyright (c) 2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_PNOR_H
#define PPC_PNV_PNOR_H

#include "hw/sysbus.h"

#define PNOR_SPI_OFFSET         0x00000000UL

#define TYPE_PNV_PNOR  "pnv-pnor"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPnor, PNV_PNOR)

struct PnvPnor {
    SysBusDevice   parent_obj;

    BlockBackend   *blk;

    uint8_t        *storage;
    uint32_t       lpc_address;
    int64_t        size;
    MemoryRegion   mmio;
};

#endif
