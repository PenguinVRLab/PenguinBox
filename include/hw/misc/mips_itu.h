/*
 * Inter-Thread Communication Unit emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIPS_ITU_H
#define MIPS_ITU_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MIPS_ITU "mips-itu"
OBJECT_DECLARE_SIMPLE_TYPE(MIPSITUState, MIPS_ITU)

#define ITC_CELL_DEPTH_SHIFT 2
#define ITC_CELL_DEPTH (1u << ITC_CELL_DEPTH_SHIFT)

typedef struct ITCStorageCell {
    struct {
        uint8_t FIFODepth;
        uint8_t FIFOPtr;
        uint8_t FIFO;
        uint8_t T;
        uint8_t F;
        uint8_t E;
    } tag;

    uint8_t fifo_out;

    uint64_t data[ITC_CELL_DEPTH];

    uint64_t blocked_threads;
} ITCStorageCell;

#define ITC_ADDRESSMAP_NUM 2

struct MIPSITUState {
    SysBusDevice parent_obj;

    uint32_t num_fifo;
    uint32_t num_semaphores;

    ITCStorageCell *cell;
    MemoryRegion storage_io;

    uint64_t ITCAddressMap[ITC_ADDRESSMAP_NUM];
    MemoryRegion tag_io;

    uint64_t icr0;
};

MemoryRegion *mips_itu_get_tag_region(MIPSITUState *itu);

#endif
