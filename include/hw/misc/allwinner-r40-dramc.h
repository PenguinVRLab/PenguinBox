/*
 * Allwinner R40 SDRAM Controller emulation
 *
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MISC_ALLWINNER_R40_DRAMC_H
#define HW_MISC_ALLWINNER_R40_DRAMC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "exec/hwaddr.h"

#define AW_R40_DRAMCOM_REGS_MAXADDR  (0x804)

#define AW_R40_DRAMCOM_REGS_NUM      (AW_R40_DRAMCOM_REGS_MAXADDR / \
                                     sizeof(uint32_t))

#define AW_R40_DRAMCTL_REGS_MAXADDR  (0x88c)

#define AW_R40_DRAMCTL_REGS_NUM      (AW_R40_DRAMCTL_REGS_MAXADDR / \
                                     sizeof(uint32_t))

#define AW_R40_DRAMPHY_REGS_MAXADDR  (0x4)

#define AW_R40_DRAMPHY_REGS_NUM      (AW_R40_DRAMPHY_REGS_MAXADDR / \
                                     sizeof(uint32_t))

#define TYPE_AW_R40_DRAMC "allwinner-r40-dramc"
OBJECT_DECLARE_SIMPLE_TYPE(AwR40DramCtlState, AW_R40_DRAMC)

struct AwR40DramCtlState {
    SysBusDevice parent_obj;

    hwaddr ram_addr;

    uint32_t ram_size;

    uint8_t set_row_bits;
    uint8_t set_bank_bits;
    uint8_t set_col_bits;

    MemoryRegion dramcom_iomem;
    MemoryRegion dramctl_iomem;
    MemoryRegion dramphy_iomem;
    MemoryRegion dram_high;
    MemoryRegion detect_cells;

    uint32_t dramcom[AW_R40_DRAMCOM_REGS_NUM];
    uint32_t dramctl[AW_R40_DRAMCTL_REGS_NUM];
    uint32_t dramphy[AW_R40_DRAMPHY_REGS_NUM] ;

};

#endif
