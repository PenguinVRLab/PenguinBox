/*
 * Allwinner Watchdog emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from Allwinner RTC,
 *  by Niek Linnenbank.
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

#ifndef HW_WATCHDOG_ALLWINNER_WDT_H
#define HW_WATCHDOG_ALLWINNER_WDT_H

#include "qom/object.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"

#define TYPE_AW_WDT    "allwinner-wdt"

#define TYPE_AW_WDT_SUN4I    TYPE_AW_WDT "-sun4i"

#define TYPE_AW_WDT_SUN6I    TYPE_AW_WDT "-sun6i"

#define AW_WDT_REGS_NUM      (5)

OBJECT_DECLARE_TYPE(AwWdtState, AwWdtClass, AW_WDT)

struct AwWdtState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    struct ptimer_state *timer;

    uint32_t regs[AW_WDT_REGS_NUM];
};

struct AwWdtClass {
    SysBusDeviceClass parent_class;

    const uint8_t *regmap;

    size_t regmap_size;

    bool (*read)(AwWdtState *s, uint32_t offset);

    bool (*write)(AwWdtState *s, uint32_t offset, uint32_t data);

    bool (*can_reset_system)(AwWdtState *s);

    bool (*is_key_valid)(AwWdtState *s, uint32_t val);

    uint8_t (*get_intv_value)(AwWdtState *s);
};

#endif
