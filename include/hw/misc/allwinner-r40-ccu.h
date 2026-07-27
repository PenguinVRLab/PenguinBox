/*
 * Allwinner R40 Clock Control Unit emulation
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

#ifndef HW_MISC_ALLWINNER_R40_CCU_H
#define HW_MISC_ALLWINNER_R40_CCU_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define AW_R40_CCU_IOSIZE        (0x400)

#define AW_R40_CCU_REGS_NUM      (AW_R40_CCU_IOSIZE / sizeof(uint32_t))

#define TYPE_AW_R40_CCU    "allwinner-r40-ccu"
OBJECT_DECLARE_SIMPLE_TYPE(AwR40ClockCtlState, AW_R40_CCU)

struct AwR40ClockCtlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t regs[AW_R40_CCU_REGS_NUM];

};

#endif
