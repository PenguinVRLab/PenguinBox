/*
 * Allwinner Sun8i Ethernet MAC emulation
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

#ifndef HW_NET_ALLWINNER_SUN8I_EMAC_H
#define HW_NET_ALLWINNER_SUN8I_EMAC_H

#include "qom/object.h"
#include "net/net.h"
#include "hw/sysbus.h"

#define TYPE_AW_SUN8I_EMAC "allwinner-sun8i-emac"
OBJECT_DECLARE_SIMPLE_TYPE(AwSun8iEmacState, AW_SUN8I_EMAC)

struct AwSun8iEmacState {
    SysBusDevice  parent_obj;

    MemoryRegion iomem;

    qemu_irq     irq;

    MemoryRegion *dma_mr;

    AddressSpace dma_as;

    NICState     *nic;

    NICConf      conf;

    uint8_t      mii_phy_addr;
    uint32_t     mii_cr;
    uint32_t     mii_st;
    uint32_t     mii_adv;

    uint32_t     basic_ctl0;
    uint32_t     basic_ctl1;
    uint32_t     int_en;
    uint32_t     int_sta;
    uint32_t     frm_flt;

    uint32_t     rx_ctl0;
    uint32_t     rx_ctl1;
    uint32_t     rx_desc_head;
    uint32_t     rx_desc_curr;

    uint32_t     tx_ctl0;
    uint32_t     tx_ctl1;
    uint32_t     tx_desc_head;
    uint32_t     tx_desc_curr;
    uint32_t     tx_flowctl;

    uint32_t     mii_cmd;
    uint32_t     mii_data;

};

#endif
