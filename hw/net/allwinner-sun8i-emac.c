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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "trace.h"
#include "net/checksum.h"
#include "qemu/module.h"
#include "exec/cpu-common.h"
#include "system/dma.h"
#include "hw/net/allwinner-sun8i-emac.h"

enum {
    REG_BASIC_CTL_0        = 0x0000,
    REG_BASIC_CTL_1        = 0x0004,
    REG_INT_STA            = 0x0008,
    REG_INT_EN             = 0x000C,
    REG_TX_CTL_0           = 0x0010,
    REG_TX_CTL_1           = 0x0014,
    REG_TX_FLOW_CTL        = 0x001C,
    REG_TX_DMA_DESC_LIST   = 0x0020,
    REG_RX_CTL_0           = 0x0024,
    REG_RX_CTL_1           = 0x0028,
    REG_RX_DMA_DESC_LIST   = 0x0034,
    REG_FRM_FLT            = 0x0038,
    REG_RX_HASH_0          = 0x0040,
    REG_RX_HASH_1          = 0x0044,
    REG_MII_CMD            = 0x0048,
    REG_MII_DATA           = 0x004C,
    REG_ADDR_HIGH          = 0x0050,
    REG_ADDR_LOW           = 0x0054,
    REG_TX_DMA_STA         = 0x00B0,
    REG_TX_CUR_DESC        = 0x00B4,
    REG_TX_CUR_BUF         = 0x00B8,
    REG_RX_DMA_STA         = 0x00C0,
    REG_RX_CUR_DESC        = 0x00C4,
    REG_RX_CUR_BUF         = 0x00C8,
    REG_RGMII_STA          = 0x00D0,
};

enum {
    BASIC_CTL0_100Mbps     = (0b11 << 2),
    BASIC_CTL0_FD          = (1 << 0),
    BASIC_CTL1_SOFTRST     = (1 << 0),
};

enum {
    INT_STA_RGMII_LINK     = (1 << 16),
    INT_STA_RX_EARLY       = (1 << 13),
    INT_STA_RX_OVERFLOW    = (1 << 12),
    INT_STA_RX_TIMEOUT     = (1 << 11),
    INT_STA_RX_DMA_STOP    = (1 << 10),
    INT_STA_RX_BUF_UA      = (1 << 9),
    INT_STA_RX             = (1 << 8),
    INT_STA_TX_EARLY       = (1 << 5),
    INT_STA_TX_UNDERFLOW   = (1 << 4),
    INT_STA_TX_TIMEOUT     = (1 << 3),
    INT_STA_TX_BUF_UA      = (1 << 2),
    INT_STA_TX_DMA_STOP    = (1 << 1),
    INT_STA_TX             = (1 << 0),
};

enum {
    INT_EN_RX_EARLY        = (1 << 13),
    INT_EN_RX_OVERFLOW     = (1 << 12),
    INT_EN_RX_TIMEOUT      = (1 << 11),
    INT_EN_RX_DMA_STOP     = (1 << 10),
    INT_EN_RX_BUF_UA       = (1 << 9),
    INT_EN_RX              = (1 << 8),
    INT_EN_TX_EARLY        = (1 << 5),
    INT_EN_TX_UNDERFLOW    = (1 << 4),
    INT_EN_TX_TIMEOUT      = (1 << 3),
    INT_EN_TX_BUF_UA       = (1 << 2),
    INT_EN_TX_DMA_STOP     = (1 << 1),
    INT_EN_TX              = (1 << 0),
};

enum {
    TX_CTL0_TX_EN          = (1 << 31),
    TX_CTL1_TX_DMA_START   = (1 << 31),
    TX_CTL1_TX_DMA_EN      = (1 << 30),
    TX_CTL1_TX_FLUSH       = (1 << 0),
};

enum {
    RX_CTL0_RX_EN          = (1 << 31),
    RX_CTL0_STRIP_FCS      = (1 << 28),
    RX_CTL0_CRC_IPV4       = (1 << 27),
};

enum {
    RX_CTL1_RX_DMA_START   = (1 << 31),
    RX_CTL1_RX_DMA_EN      = (1 << 30),
    RX_CTL1_RX_MD          = (1 << 1),
};

enum {
    RX_FRM_FLT_DIS_ADDR    = (1 << 31),
};

enum {
    MII_CMD_PHY_ADDR_SHIFT = (12),
    MII_CMD_PHY_ADDR_MASK  = (0xf000),
    MII_CMD_PHY_REG_SHIFT  = (4),
    MII_CMD_PHY_REG_MASK   = (0xf0),
    MII_CMD_PHY_RW         = (1 << 1),
    MII_CMD_PHY_BUSY       = (1 << 0),
};

enum {
    TX_DMA_STA_STOP        = (0b000),
    TX_DMA_STA_RUN_FETCH   = (0b001),
    TX_DMA_STA_WAIT_STA    = (0b010),
};

enum {
    RX_DMA_STA_STOP        = (0b000),
    RX_DMA_STA_RUN_FETCH   = (0b001),
    RX_DMA_STA_WAIT_FRM    = (0b011),
};

enum {
    REG_BASIC_CTL_1_RST    = 0x08000000,
};

enum {
    AW_SUN8I_EMAC_MIN_PKT_SZ  = 64
};

typedef struct FrameDescriptor {
    uint32_t status;
    uint32_t status2;
    uint32_t addr;
    uint32_t next;
} FrameDescriptor;

enum {
    DESC_STATUS_CTL                 = (1 << 31),
    DESC_STATUS2_BUF_SIZE_MASK      = (0x7ff),
};

enum {
    TX_DESC_STATUS_LENGTH_ERR       = (1 << 14),
    TX_DESC_STATUS2_FIRST_DESC      = (1 << 29),
    TX_DESC_STATUS2_LAST_DESC       = (1 << 30),
    TX_DESC_STATUS2_CHECKSUM_MASK   = (0x3 << 27),
};

enum {
    RX_DESC_STATUS_FIRST_DESC       = (1 << 9),
    RX_DESC_STATUS_LAST_DESC        = (1 << 8),
    RX_DESC_STATUS_FRM_LEN_MASK     = (0x3fff0000),
    RX_DESC_STATUS_FRM_LEN_SHIFT    = (16),
    RX_DESC_STATUS_NO_BUF           = (1 << 14),
    RX_DESC_STATUS_HEADER_ERR       = (1 << 7),
    RX_DESC_STATUS_LENGTH_ERR       = (1 << 4),
    RX_DESC_STATUS_CRC_ERR          = (1 << 1),
    RX_DESC_STATUS_PAYLOAD_ERR      = (1 << 0),
    RX_DESC_STATUS2_RX_INT_CTL      = (1 << 31),
};

enum {
    MII_REG_CR                      = (0x0),
    MII_REG_ST                      = (0x1),
    MII_REG_ID_HIGH                 = (0x2),
    MII_REG_ID_LOW                  = (0x3),
    MII_REG_ADV                     = (0x4),
    MII_REG_LPA                     = (0x5),
};

enum {
    MII_REG_CR_RESET                = (1 << 15),
    MII_REG_CR_POWERDOWN            = (1 << 11),
    MII_REG_CR_10Mbit               = (0),
    MII_REG_CR_100Mbit              = (1 << 13),
    MII_REG_CR_1000Mbit             = (1 << 6),
    MII_REG_CR_AUTO_NEG             = (1 << 12),
    MII_REG_CR_AUTO_NEG_RESTART     = (1 << 9),
    MII_REG_CR_FULLDUPLEX           = (1 << 8),
};

enum {
    MII_REG_ST_100BASE_T4           = (1 << 15),
    MII_REG_ST_100BASE_X_FD         = (1 << 14),
    MII_REG_ST_100BASE_X_HD         = (1 << 13),
    MII_REG_ST_10_FD                = (1 << 12),
    MII_REG_ST_10_HD                = (1 << 11),
    MII_REG_ST_100BASE_T2_FD        = (1 << 10),
    MII_REG_ST_100BASE_T2_HD        = (1 << 9),
    MII_REG_ST_AUTONEG_COMPLETE     = (1 << 5),
    MII_REG_ST_AUTONEG_AVAIL        = (1 << 3),
    MII_REG_ST_LINK_UP              = (1 << 2),
};

enum {
    MII_REG_LPA_10_HD               = (1 << 5),
    MII_REG_LPA_10_FD               = (1 << 6),
    MII_REG_LPA_100_HD              = (1 << 7),
    MII_REG_LPA_100_FD              = (1 << 8),
    MII_REG_LPA_PAUSE               = (1 << 10),
    MII_REG_LPA_ASYMPAUSE           = (1 << 11),
};

enum {
    MII_PHY_ID_HIGH                 = 0x0044,
    MII_PHY_ID_LOW                  = 0x1400,
};

static void allwinner_sun8i_emac_mii_set_link(AwSun8iEmacState *s,
                                              bool link_active)
{
    if (link_active) {
        s->mii_st |= MII_REG_ST_LINK_UP;
    } else {
        s->mii_st &= ~MII_REG_ST_LINK_UP;
    }
}

static void allwinner_sun8i_emac_mii_reset(AwSun8iEmacState *s,
                                           bool link_active)
{
    s->mii_cr = MII_REG_CR_100Mbit | MII_REG_CR_AUTO_NEG |
                MII_REG_CR_FULLDUPLEX;
    s->mii_st = MII_REG_ST_100BASE_T4 | MII_REG_ST_100BASE_X_FD |
                MII_REG_ST_100BASE_X_HD | MII_REG_ST_10_FD | MII_REG_ST_10_HD |
                MII_REG_ST_100BASE_T2_FD | MII_REG_ST_100BASE_T2_HD |
                MII_REG_ST_AUTONEG_COMPLETE | MII_REG_ST_AUTONEG_AVAIL;
    s->mii_adv = 0;

    allwinner_sun8i_emac_mii_set_link(s, link_active);
}

static void allwinner_sun8i_emac_mii_cmd(AwSun8iEmacState *s)
{
    uint8_t addr, reg;

    addr = (s->mii_cmd & MII_CMD_PHY_ADDR_MASK) >> MII_CMD_PHY_ADDR_SHIFT;
    reg = (s->mii_cmd & MII_CMD_PHY_REG_MASK) >> MII_CMD_PHY_REG_SHIFT;

    if (addr != s->mii_phy_addr) {
        return;
    }

    if (s->mii_cmd & MII_CMD_PHY_RW) {
        trace_allwinner_sun8i_emac_mii_write_reg(reg, s->mii_data);

        switch (reg) {
        case MII_REG_CR:
            if (s->mii_data & MII_REG_CR_RESET) {
                allwinner_sun8i_emac_mii_reset(s, s->mii_st &
                                                  MII_REG_ST_LINK_UP);
            } else {
                s->mii_cr = s->mii_data & ~(MII_REG_CR_RESET |
                                            MII_REG_CR_AUTO_NEG_RESTART);
            }
            break;
        case MII_REG_ADV:
            s->mii_adv = s->mii_data;
            break;
        case MII_REG_ID_HIGH:
        case MII_REG_ID_LOW:
        case MII_REG_LPA:
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "allwinner-h3-emac: write access to "
                                     "unknown MII register 0x%x\n", reg);
            break;
        }
    } else {
        switch (reg) {
        case MII_REG_CR:
            s->mii_data = s->mii_cr;
            break;
        case MII_REG_ST:
            s->mii_data = s->mii_st;
            break;
        case MII_REG_ID_HIGH:
            s->mii_data = MII_PHY_ID_HIGH;
            break;
        case MII_REG_ID_LOW:
            s->mii_data = MII_PHY_ID_LOW;
            break;
        case MII_REG_ADV:
            s->mii_data = s->mii_adv;
            break;
        case MII_REG_LPA:
            s->mii_data = MII_REG_LPA_10_HD | MII_REG_LPA_10_FD |
                          MII_REG_LPA_100_HD | MII_REG_LPA_100_FD |
                          MII_REG_LPA_PAUSE | MII_REG_LPA_ASYMPAUSE;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "allwinner-h3-emac: read access to "
                                     "unknown MII register 0x%x\n", reg);
            s->mii_data = 0;
            break;
        }

        trace_allwinner_sun8i_emac_mii_read_reg(reg, s->mii_data);
    }
}

static void allwinner_sun8i_emac_update_irq(AwSun8iEmacState *s)
{
    qemu_set_irq(s->irq, (s->int_sta & s->int_en) != 0);
}

static bool allwinner_sun8i_emac_desc_owned(FrameDescriptor *desc,
                                            size_t min_buf_size)
{
    return (desc->status & DESC_STATUS_CTL) && (min_buf_size == 0 ||
           (desc->status2 & DESC_STATUS2_BUF_SIZE_MASK) >= min_buf_size);
}

static void allwinner_sun8i_emac_get_desc(AwSun8iEmacState *s,
                                          FrameDescriptor *desc,
                                          uint32_t phys_addr)
{
    uint32_t desc_words[4];
    dma_memory_read(&s->dma_as, phys_addr, &desc_words, sizeof(desc_words),
                    MEMTXATTRS_UNSPECIFIED);
    desc->status = le32_to_cpu(desc_words[0]);
    desc->status2 = le32_to_cpu(desc_words[1]);
    desc->addr = le32_to_cpu(desc_words[2]);
    desc->next = le32_to_cpu(desc_words[3]);
}

static uint32_t allwinner_sun8i_emac_next_desc(AwSun8iEmacState *s,
                                               FrameDescriptor *desc)
{
    const uint32_t nxt = desc->next;
    allwinner_sun8i_emac_get_desc(s, desc, nxt);
    return nxt;
}

static uint32_t allwinner_sun8i_emac_find_desc(AwSun8iEmacState *s,
                                               FrameDescriptor *desc,
                                               uint32_t start_addr,
                                               size_t min_size)
{
    uint32_t desc_addr = start_addr;

    while (desc_addr != 0) {
        allwinner_sun8i_emac_get_desc(s, desc, desc_addr);

        if (allwinner_sun8i_emac_desc_owned(desc, min_size)) {
            return desc_addr;
        } else if (desc->next == start_addr) {
            break;
        } else {
            desc_addr = desc->next;
        }
    }

    return 0;
}

static uint32_t allwinner_sun8i_emac_rx_desc(AwSun8iEmacState *s,
                                             FrameDescriptor *desc,
                                             size_t min_size)
{
    return allwinner_sun8i_emac_find_desc(s, desc, s->rx_desc_curr, min_size);
}

static uint32_t allwinner_sun8i_emac_tx_desc(AwSun8iEmacState *s,
                                             FrameDescriptor *desc)
{
    allwinner_sun8i_emac_get_desc(s, desc, s->tx_desc_curr);
    return s->tx_desc_curr;
}

static void allwinner_sun8i_emac_flush_desc(AwSun8iEmacState *s,
                                            const FrameDescriptor *desc,
                                            uint32_t phys_addr)
{
    uint32_t desc_words[4];
    desc_words[0] = cpu_to_le32(desc->status);
    desc_words[1] = cpu_to_le32(desc->status2);
    desc_words[2] = cpu_to_le32(desc->addr);
    desc_words[3] = cpu_to_le32(desc->next);
    dma_memory_write(&s->dma_as, phys_addr, &desc_words, sizeof(desc_words),
                     MEMTXATTRS_UNSPECIFIED);
}

static bool allwinner_sun8i_emac_can_receive(NetClientState *nc)
{
    AwSun8iEmacState *s = qemu_get_nic_opaque(nc);
    FrameDescriptor desc;

    return (s->rx_ctl0 & RX_CTL0_RX_EN) &&
           (allwinner_sun8i_emac_rx_desc(s, &desc, 0) != 0);
}

static ssize_t allwinner_sun8i_emac_receive(NetClientState *nc,
                                            const uint8_t *buf,
                                            size_t size)
{
    AwSun8iEmacState *s = qemu_get_nic_opaque(nc);
    FrameDescriptor desc;
    size_t bytes_left = size;
    size_t desc_bytes = 0;
    size_t pad_fcs_size = 4;
    size_t padding = 0;

    if (!(s->rx_ctl0 & RX_CTL0_RX_EN)) {
        return -1;
    }

    s->rx_desc_curr = allwinner_sun8i_emac_rx_desc(s, &desc,
                                                   AW_SUN8I_EMAC_MIN_PKT_SZ);
    if (!s->rx_desc_curr) {
        s->int_sta |= INT_STA_RX_BUF_UA;
    }

    while (s->rx_desc_curr && bytes_left > 0) {
        desc.status &= ~DESC_STATUS_CTL;
        desc.status &= ~RX_DESC_STATUS_FRM_LEN_MASK;

        if (bytes_left == size) {
            desc.status |= RX_DESC_STATUS_FIRST_DESC;
        }

        if ((desc.status2 & DESC_STATUS2_BUF_SIZE_MASK) <
            (bytes_left + pad_fcs_size)) {
            desc_bytes = desc.status2 & DESC_STATUS2_BUF_SIZE_MASK;
            desc.status |= desc_bytes << RX_DESC_STATUS_FRM_LEN_SHIFT;
        } else {
            padding = pad_fcs_size;
            if (bytes_left < AW_SUN8I_EMAC_MIN_PKT_SZ) {
                padding += (AW_SUN8I_EMAC_MIN_PKT_SZ - bytes_left);
            }

            desc_bytes = (bytes_left);
            desc.status |= RX_DESC_STATUS_LAST_DESC;
            desc.status |= (bytes_left + padding)
                            << RX_DESC_STATUS_FRM_LEN_SHIFT;
        }

        dma_memory_write(&s->dma_as, desc.addr, buf, desc_bytes,
                         MEMTXATTRS_UNSPECIFIED);
        allwinner_sun8i_emac_flush_desc(s, &desc, s->rx_desc_curr);
        trace_allwinner_sun8i_emac_receive(s->rx_desc_curr, desc.addr,
                                           desc_bytes);

        if (!(desc.status2 & RX_DESC_STATUS2_RX_INT_CTL)) {
            s->int_sta |= INT_STA_RX;
        }

        buf += desc_bytes;
        bytes_left -= desc_bytes;

        s->rx_desc_curr = allwinner_sun8i_emac_find_desc(s, &desc, desc.next,
                                                         AW_SUN8I_EMAC_MIN_PKT_SZ);
        if (!s->rx_desc_curr) {
            s->int_sta |= INT_STA_RX_BUF_UA;
            s->rx_desc_curr = s->rx_desc_head;
            break;
        }
    }

    s->rx_ctl1 &= ~RX_CTL1_RX_DMA_START;
    allwinner_sun8i_emac_update_irq(s);

    return size;
}

static void allwinner_sun8i_emac_transmit(AwSun8iEmacState *s)
{
    NetClientState *nc = qemu_get_queue(s->nic);
    FrameDescriptor desc;
    size_t bytes = 0;
    size_t packet_bytes = 0;
    size_t transmitted = 0;
    static uint8_t packet_buf[2048];

    s->tx_desc_curr = allwinner_sun8i_emac_tx_desc(s, &desc);

    while (allwinner_sun8i_emac_desc_owned(&desc, 0)) {

        bytes = desc.status2 & DESC_STATUS2_BUF_SIZE_MASK;
        if (bytes + packet_bytes > sizeof(packet_buf)) {
            desc.status |= TX_DESC_STATUS_LENGTH_ERR;
            break;
        }
        dma_memory_read(&s->dma_as, desc.addr, packet_buf + packet_bytes,
                        bytes, MEMTXATTRS_UNSPECIFIED);
        packet_bytes += bytes;
        desc.status &= ~DESC_STATUS_CTL;
        allwinner_sun8i_emac_flush_desc(s, &desc, s->tx_desc_curr);

        if (desc.status2 & TX_DESC_STATUS2_LAST_DESC) {
            if (desc.status2 & TX_DESC_STATUS2_CHECKSUM_MASK) {
                net_checksum_calculate(packet_buf, packet_bytes, CSUM_ALL);
            }

            qemu_send_packet(nc, packet_buf, packet_bytes);
            trace_allwinner_sun8i_emac_transmit(s->tx_desc_curr, desc.addr,
                                                bytes);

            packet_bytes = 0;
            transmitted++;
        }
        s->tx_desc_curr = allwinner_sun8i_emac_next_desc(s, &desc);
    }

    if (transmitted > 0) {
        s->int_sta |= INT_STA_TX;
        s->tx_ctl1 &= ~TX_CTL1_TX_DMA_START;
        allwinner_sun8i_emac_update_irq(s);
    }
}

static void allwinner_sun8i_emac_reset(DeviceState *dev)
{
    AwSun8iEmacState *s = AW_SUN8I_EMAC(dev);
    NetClientState *nc = qemu_get_queue(s->nic);

    trace_allwinner_sun8i_emac_reset();

    s->mii_cmd = 0;
    s->mii_data = 0;
    s->basic_ctl0 = 0;
    s->basic_ctl1 = REG_BASIC_CTL_1_RST;
    s->int_en = 0;
    s->int_sta = 0;
    s->frm_flt = 0;
    s->rx_ctl0 = 0;
    s->rx_ctl1 = RX_CTL1_RX_MD;
    s->rx_desc_head = 0;
    s->rx_desc_curr = 0;
    s->tx_ctl0 = 0;
    s->tx_ctl1 = 0;
    s->tx_desc_head = 0;
    s->tx_desc_curr = 0;
    s->tx_flowctl = 0;

    allwinner_sun8i_emac_mii_reset(s, !nc->link_down);
}

static uint64_t allwinner_sun8i_emac_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    AwSun8iEmacState *s = AW_SUN8I_EMAC(opaque);
    uint64_t value = 0;
    FrameDescriptor desc;

    switch (offset) {
    case REG_BASIC_CTL_0:
        value = s->basic_ctl0;
        break;
    case REG_BASIC_CTL_1:
        value = s->basic_ctl1;
        break;
    case REG_INT_STA:
        value = s->int_sta;
        break;
    case REG_INT_EN:
        value = s->int_en;
        break;
    case REG_TX_CTL_0:
        value = s->tx_ctl0;
        break;
    case REG_TX_CTL_1:
        value = s->tx_ctl1;
        break;
    case REG_TX_FLOW_CTL:
        value = s->tx_flowctl;
        break;
    case REG_TX_DMA_DESC_LIST:
        value = s->tx_desc_head;
        break;
    case REG_RX_CTL_0:
        value = s->rx_ctl0;
        break;
    case REG_RX_CTL_1:
        value = s->rx_ctl1;
        break;
    case REG_RX_DMA_DESC_LIST:
        value = s->rx_desc_head;
        break;
    case REG_FRM_FLT:
        value = s->frm_flt;
        break;
    case REG_RX_HASH_0:
    case REG_RX_HASH_1:
        break;
    case REG_MII_CMD:
        value = s->mii_cmd;
        break;
    case REG_MII_DATA:
        value = s->mii_data;
        break;
    case REG_ADDR_HIGH:
        value = lduw_le_p(s->conf.macaddr.a + 4);
        break;
    case REG_ADDR_LOW:
        value = ldl_le_p(s->conf.macaddr.a);
        break;
    case REG_TX_DMA_STA:
        break;
    case REG_TX_CUR_DESC:
        value = s->tx_desc_curr;
        break;
    case REG_TX_CUR_BUF:
        if (s->tx_desc_curr != 0) {
            allwinner_sun8i_emac_get_desc(s, &desc, s->tx_desc_curr);
            value = desc.addr;
        } else {
            value = 0;
        }
        break;
    case REG_RX_DMA_STA:
        break;
    case REG_RX_CUR_DESC:
        value = s->rx_desc_curr;
        break;
    case REG_RX_CUR_BUF:
        if (s->rx_desc_curr != 0) {
            allwinner_sun8i_emac_get_desc(s, &desc, s->rx_desc_curr);
            value = desc.addr;
        } else {
            value = 0;
        }
        break;
    case REG_RGMII_STA:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "allwinner-h3-emac: read access to unknown "
                                 "EMAC register 0x" HWADDR_FMT_plx "\n",
                                  offset);
    }

    trace_allwinner_sun8i_emac_read(offset, value);
    return value;
}

static void allwinner_sun8i_emac_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size)
{
    AwSun8iEmacState *s = AW_SUN8I_EMAC(opaque);
    NetClientState *nc = qemu_get_queue(s->nic);

    trace_allwinner_sun8i_emac_write(offset, value);

    switch (offset) {
    case REG_BASIC_CTL_0:
        s->basic_ctl0 = value;
        break;
    case REG_BASIC_CTL_1:
        if (value & BASIC_CTL1_SOFTRST) {
            allwinner_sun8i_emac_reset(DEVICE(s));
            value &= ~BASIC_CTL1_SOFTRST;
        }
        s->basic_ctl1 = value;
        if (allwinner_sun8i_emac_can_receive(nc)) {
            qemu_flush_queued_packets(nc);
        }
        break;
    case REG_INT_STA:
        s->int_sta &= ~value;
        allwinner_sun8i_emac_update_irq(s);
        break;
    case REG_INT_EN:
        s->int_en = value;
        allwinner_sun8i_emac_update_irq(s);
        break;
    case REG_TX_CTL_0:
        s->tx_ctl0 = value;
        break;
    case REG_TX_CTL_1:
        s->tx_ctl1 = value;
        if (value & TX_CTL1_TX_DMA_EN) {
            allwinner_sun8i_emac_transmit(s);
        }
        break;
    case REG_TX_FLOW_CTL:
        s->tx_flowctl = value;
        break;
    case REG_TX_DMA_DESC_LIST:
        s->tx_desc_head = value;
        s->tx_desc_curr = value;
        break;
    case REG_RX_CTL_0:
        s->rx_ctl0 = value;
        break;
    case REG_RX_CTL_1:
        s->rx_ctl1 = value | RX_CTL1_RX_MD;
        if ((value & RX_CTL1_RX_DMA_EN) &&
             allwinner_sun8i_emac_can_receive(nc)) {
            qemu_flush_queued_packets(nc);
        }
        break;
    case REG_RX_DMA_DESC_LIST:
        s->rx_desc_head = value;
        s->rx_desc_curr = value;
        break;
    case REG_FRM_FLT:
        s->frm_flt = value;
        break;
    case REG_RX_HASH_0:
    case REG_RX_HASH_1:
        break;
    case REG_MII_CMD:
        s->mii_cmd = value & ~MII_CMD_PHY_BUSY;
        allwinner_sun8i_emac_mii_cmd(s);
        break;
    case REG_MII_DATA:
        s->mii_data = value;
        break;
    case REG_ADDR_HIGH:
        stw_le_p(s->conf.macaddr.a + 4, value);
        break;
    case REG_ADDR_LOW:
        stl_le_p(s->conf.macaddr.a, value);
        break;
    case REG_TX_DMA_STA:
    case REG_TX_CUR_DESC:
    case REG_TX_CUR_BUF:
    case REG_RX_DMA_STA:
    case REG_RX_CUR_DESC:
    case REG_RX_CUR_BUF:
    case REG_RGMII_STA:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "allwinner-h3-emac: write access to unknown "
                                 "EMAC register 0x" HWADDR_FMT_plx "\n",
                                  offset);
    }
}

static void allwinner_sun8i_emac_set_link(NetClientState *nc)
{
    AwSun8iEmacState *s = qemu_get_nic_opaque(nc);

    trace_allwinner_sun8i_emac_set_link(!nc->link_down);
    allwinner_sun8i_emac_mii_set_link(s, !nc->link_down);
}

static const MemoryRegionOps allwinner_sun8i_emac_mem_ops = {
    .read = allwinner_sun8i_emac_read,
    .write = allwinner_sun8i_emac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static NetClientInfo net_allwinner_sun8i_emac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = allwinner_sun8i_emac_can_receive,
    .receive = allwinner_sun8i_emac_receive,
    .link_status_changed = allwinner_sun8i_emac_set_link,
};

static void allwinner_sun8i_emac_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwSun8iEmacState *s = AW_SUN8I_EMAC(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_sun8i_emac_mem_ops,
                           s, TYPE_AW_SUN8I_EMAC, 64 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void allwinner_sun8i_emac_realize(DeviceState *dev, Error **errp)
{
    AwSun8iEmacState *s = AW_SUN8I_EMAC(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_AW_SUN8I_EMAC " 'dma-memory' link not set");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, "emac-dma");

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_allwinner_sun8i_emac_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static const Property allwinner_sun8i_emac_properties[] = {
    DEFINE_NIC_PROPERTIES(AwSun8iEmacState, conf),
    DEFINE_PROP_UINT8("phy-addr", AwSun8iEmacState, mii_phy_addr, 0),
    DEFINE_PROP_LINK("dma-memory", AwSun8iEmacState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static int allwinner_sun8i_emac_post_load(void *opaque, int version_id)
{
    AwSun8iEmacState *s = opaque;

    allwinner_sun8i_emac_set_link(qemu_get_queue(s->nic));

    return 0;
}

static const VMStateDescription vmstate_aw_emac = {
    .name = "allwinner-sun8i-emac",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = allwinner_sun8i_emac_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(mii_phy_addr, AwSun8iEmacState),
        VMSTATE_UINT32(mii_cmd, AwSun8iEmacState),
        VMSTATE_UINT32(mii_data, AwSun8iEmacState),
        VMSTATE_UINT32(mii_cr, AwSun8iEmacState),
        VMSTATE_UINT32(mii_st, AwSun8iEmacState),
        VMSTATE_UINT32(mii_adv, AwSun8iEmacState),
        VMSTATE_UINT32(basic_ctl0, AwSun8iEmacState),
        VMSTATE_UINT32(basic_ctl1, AwSun8iEmacState),
        VMSTATE_UINT32(int_en, AwSun8iEmacState),
        VMSTATE_UINT32(int_sta, AwSun8iEmacState),
        VMSTATE_UINT32(frm_flt, AwSun8iEmacState),
        VMSTATE_UINT32(rx_ctl0, AwSun8iEmacState),
        VMSTATE_UINT32(rx_ctl1, AwSun8iEmacState),
        VMSTATE_UINT32(rx_desc_head, AwSun8iEmacState),
        VMSTATE_UINT32(rx_desc_curr, AwSun8iEmacState),
        VMSTATE_UINT32(tx_ctl0, AwSun8iEmacState),
        VMSTATE_UINT32(tx_ctl1, AwSun8iEmacState),
        VMSTATE_UINT32(tx_desc_head, AwSun8iEmacState),
        VMSTATE_UINT32(tx_desc_curr, AwSun8iEmacState),
        VMSTATE_UINT32(tx_flowctl, AwSun8iEmacState),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_sun8i_emac_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = allwinner_sun8i_emac_realize;
    device_class_set_legacy_reset(dc, allwinner_sun8i_emac_reset);
    dc->vmsd = &vmstate_aw_emac;
    device_class_set_props(dc, allwinner_sun8i_emac_properties);
}

static const TypeInfo allwinner_sun8i_emac_info = {
    .name           = TYPE_AW_SUN8I_EMAC,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AwSun8iEmacState),
    .instance_init  = allwinner_sun8i_emac_init,
    .class_init     = allwinner_sun8i_emac_class_init,
};

static void allwinner_sun8i_emac_register_types(void)
{
    type_register_static(&allwinner_sun8i_emac_info);
}

type_init(allwinner_sun8i_emac_register_types)
