/*
 * Allwinner SRAM controller emulation
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

#ifndef HW_MISC_ALLWINNER_SRAMC_H
#define HW_MISC_ALLWINNER_SRAMC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "qemu/uuid.h"

#define TYPE_AW_SRAMC               "allwinner-sramc"
#define TYPE_AW_SRAMC_SUN8I_R40     TYPE_AW_SRAMC "-sun8i-r40"
OBJECT_DECLARE_TYPE(AwSRAMCState, AwSRAMCClass, AW_SRAMC)

struct AwSRAMCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t sram_ctl1;
    uint32_t sram_ver;
    uint32_t sram_soft_entry_reg0;
};

struct AwSRAMCClass {
    SysBusDeviceClass parent_class;

    uint32_t sram_version_code;
};

#endif
