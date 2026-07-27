/*
 * Allwinner (sun4i and above) SD Host Controller emulation
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

#ifndef HW_SD_ALLWINNER_SDHOST_H
#define HW_SD_ALLWINNER_SDHOST_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"

#define TYPE_AW_SDHOST "allwinner-sdhost"

#define TYPE_AW_SDHOST_SUN4I TYPE_AW_SDHOST "-sun4i"

#define TYPE_AW_SDHOST_SUN5I TYPE_AW_SDHOST "-sun5i"

#define TYPE_AW_SDHOST_SUN50I_A64 TYPE_AW_SDHOST "-sun50i-a64"

#define TYPE_AW_SDHOST_SUN50I_A64_EMMC  TYPE_AW_SDHOST "-sun50i-a64-emmc"

OBJECT_DECLARE_TYPE(AwSdHostState, AwSdHostClass, AW_SDHOST)

struct AwSdHostState {
    SysBusDevice busdev;

    SDBus sdbus;

    MemoryRegion iomem;

    qemu_irq irq;

    MemoryRegion *dma_mr;

    AddressSpace dma_as;

    uint32_t transfer_cnt;

    uint32_t global_ctl;
    uint32_t clock_ctl;
    uint32_t timeout;
    uint32_t bus_width;
    uint32_t block_size;
    uint32_t byte_count;

    uint32_t command;
    uint32_t command_arg;
    uint32_t response[4];

    uint32_t irq_mask;
    uint32_t irq_status;
    uint32_t status;

    uint32_t fifo_wlevel;
    uint32_t fifo_func_sel;
    uint32_t debug_enable;
    uint32_t auto12_arg;
    uint32_t newtiming_set;
    uint32_t newtiming_debug;
    uint32_t hardware_rst;
    uint32_t dmac;
    uint32_t desc_base;
    uint32_t dmac_status;
    uint32_t dmac_irq;
    uint32_t card_threshold;
    uint32_t startbit_detect;
    uint32_t response_crc;
    uint32_t data_crc[8];
    uint32_t sample_delay;
    uint32_t status_crc;

};

struct AwSdHostClass {
    SysBusDeviceClass parent_class;

    size_t max_desc_size;
    bool   is_sun4i;

    bool can_calibrate;
};

#endif
