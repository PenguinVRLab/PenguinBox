/*
 * Allwinner Real Time Clock emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
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

#ifndef HW_MISC_ALLWINNER_RTC_H
#define HW_MISC_ALLWINNER_RTC_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define AW_RTC_REGS_MAXADDR     (0x200)

#define AW_RTC_REGS_NUM         (AW_RTC_REGS_MAXADDR / sizeof(uint32_t))

#define TYPE_AW_RTC          "allwinner-rtc"

#define TYPE_AW_RTC_SUN4I    TYPE_AW_RTC "-sun4i"

#define TYPE_AW_RTC_SUN6I    TYPE_AW_RTC "-sun6i"

#define TYPE_AW_RTC_SUN7I    TYPE_AW_RTC "-sun7i"

OBJECT_DECLARE_TYPE(AwRtcState, AwRtcClass, AW_RTC)

struct AwRtcState {
    SysBusDevice parent_obj;

    int base_year;

    MemoryRegion iomem;

    uint32_t regs[AW_RTC_REGS_NUM];

};

struct AwRtcClass {
    SysBusDeviceClass parent_class;

    const uint8_t *regmap;

    size_t regmap_size;

    bool (*read)(AwRtcState *s, uint32_t offset);

    bool (*write)(AwRtcState *s, uint32_t offset, uint32_t data);

};

#endif
