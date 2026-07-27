/*
 * QEMU NE2000 emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "net/eth.h"
#include "qemu/module.h"
#include "system/memory.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "ne2000.h"
#include "trace.h"

#define MAX_ETH_FRAME_SIZE 1514

#define E8390_CMD       0x00
#define EN0_CLDALO      0x01
#define EN0_STARTPG     0x01
#define EN0_CLDAHI      0x02
#define EN0_STOPPG      0x02
#define EN0_BOUNDARY    0x03
#define EN0_TSR         0x04
#define EN0_TPSR        0x04
#define EN0_NCR         0x05
#define EN0_TCNTLO      0x05
#define EN0_FIFO        0x06
#define EN0_TCNTHI      0x06
#define EN0_ISR         0x07
#define EN0_CRDALO      0x08
#define EN0_RSARLO      0x08
#define EN0_CRDAHI      0x09
#define EN0_RSARHI      0x09
#define EN0_RCNTLO      0x0a
#define EN0_RTL8029ID0  0x0a
#define EN0_RCNTHI      0x0b
#define EN0_RTL8029ID1  0x0b
#define EN0_RSR         0x0c
#define EN0_RXCR        0x0c
#define EN0_TXCR        0x0d
#define EN0_COUNTER0    0x0d
#define EN0_DCFG        0x0e
#define EN0_COUNTER1    0x0e
#define EN0_IMR         0x0f
#define EN0_COUNTER2    0x0f

#define EN1_PHYS        0x11
#define EN1_CURPAG      0x17
#define EN1_MULT        0x18

#define EN2_STARTPG     0x21
#define EN2_STOPPG      0x22

#define EN3_CONFIG0     0x33
#define EN3_CONFIG1     0x34
#define EN3_CONFIG2     0x35
#define EN3_CONFIG3     0x36

#define E8390_STOP      0x01
#define E8390_START     0x02
#define E8390_TRANS     0x04
#define E8390_RREAD     0x08
#define E8390_RWRITE    0x10
#define E8390_NODMA     0x20
#define E8390_PAGE0     0x00
#define E8390_PAGE1     0x40
#define E8390_PAGE2     0x80

#define ENISR_RX        0x01
#define ENISR_TX        0x02
#define ENISR_RX_ERR    0x04
#define ENISR_TX_ERR    0x08
#define ENISR_OVER      0x10
#define ENISR_COUNTERS  0x20
#define ENISR_RDC       0x40
#define ENISR_RESET     0x80
#define ENISR_ALL       0x3f

#define ENRSR_RXOK      0x01
#define ENRSR_CRC       0x02
#define ENRSR_FAE       0x04
#define ENRSR_FO        0x08
#define ENRSR_MPA       0x10
#define ENRSR_PHY       0x20
#define ENRSR_DIS       0x40
#define ENRSR_DEF       0x80

#define ENTSR_PTX 0x01
#define ENTSR_ND  0x02
#define ENTSR_COL 0x04
#define ENTSR_ABT 0x08
#define ENTSR_CRS 0x10
#define ENTSR_FU  0x20
#define ENTSR_CDH 0x40
#define ENTSR_OWC 0x80

void ne2000_reset(NE2000State *s)
{
    int i;

    s->isr = ENISR_RESET;
    memcpy(s->mem, &s->c.macaddr, 6);
    s->mem[14] = 0x57;
    s->mem[15] = 0x57;

    for(i = 15;i >= 0; i--) {
        s->mem[2 * i] = s->mem[i];
        s->mem[2 * i + 1] = s->mem[i];
    }
}

static void ne2000_update_irq(NE2000State *s)
{
    int isr;
    isr = (s->isr & s->imr) & 0x7f;
#if defined(DEBUG_NE2000)
    printf("NE2000: Set IRQ to %d (%02x %02x)\n",
           isr ? 1 : 0, s->isr, s->imr);
#endif
    qemu_set_irq(s->irq, (isr != 0));
}

static int ne2000_buffer_full(NE2000State *s)
{
    int avail, index, boundary;

    if (s->stop <= s->start) {
        return 1;
    }

    index = s->curpag << 8;
    boundary = s->boundary << 8;
    if (index < boundary)
        avail = boundary - index;
    else
        avail = (s->stop - s->start) - (index - boundary);
    if (avail < (MAX_ETH_FRAME_SIZE + 4))
        return 1;
    return 0;
}

ssize_t ne2000_receive(NetClientState *nc, const uint8_t *buf, size_t size_)
{
    NE2000State *s = qemu_get_nic_opaque(nc);
    size_t size = size_;
    uint8_t *p;
    unsigned int total_len, next, avail, len, index, mcast_idx;
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#if defined(DEBUG_NE2000)
    printf("NE2000: received len=%zu\n", size);
#endif

    if (s->cmd & E8390_STOP || ne2000_buffer_full(s))
        return -1;

    if (s->rxcr & 0x10) {
    } else {
        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            if (!(s->rxcr & 0x04))
                return size;
        } else if (buf[0] & 0x01) {
            if (!(s->rxcr & 0x08))
                return size;
            mcast_idx = net_crc32(buf, ETH_ALEN) >> 26;
            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))))
                return size;
        } else if (s->mem[0] == buf[0] &&
                   s->mem[2] == buf[1] &&
                   s->mem[4] == buf[2] &&
                   s->mem[6] == buf[3] &&
                   s->mem[8] == buf[4] &&
                   s->mem[10] == buf[5]) {
        } else {
            return size;
        }
    }

    index = s->curpag << 8;
    if (index >= NE2000_PMEM_END) {
        index = s->start;
    }
    total_len = size + 4;
    next = index + ((total_len + 4 + 255) & ~0xff);
    if (next >= s->stop)
        next -= (s->stop - s->start);
    p = s->mem + index;
    s->rsr = ENRSR_RXOK;
    if (buf[0] & 0x01)
        s->rsr |= ENRSR_PHY;
    p[0] = s->rsr;
    p[1] = next >> 8;
    p[2] = total_len;
    p[3] = total_len >> 8;
    index += 4;

    while (size > 0) {
        if (index <= s->stop)
            avail = s->stop - index;
        else
            break;
        len = size;
        if (len > avail)
            len = avail;
        memcpy(s->mem + index, buf, len);
        buf += len;
        index += len;
        if (index == s->stop)
            index = s->start;
        size -= len;
    }
    s->curpag = next >> 8;

    s->isr |= ENISR_RX;
    ne2000_update_irq(s);

    return size_;
}

static void ne2000_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    NE2000State *s = opaque;
    int offset, page, index;

    addr &= 0xf;
    trace_ne2000_ioport_write(addr, val);
    if (addr == E8390_CMD) {
        s->cmd = val;
        if (!(val & E8390_STOP)) {
            s->isr &= ~ENISR_RESET;
            if ((val & (E8390_RREAD | E8390_RWRITE)) &&
                s->rcnt == 0) {
                s->isr |= ENISR_RDC;
                ne2000_update_irq(s);
            }
            if (val & E8390_TRANS) {
                index = (s->tpsr << 8);
                if (index >= NE2000_PMEM_END)
                    index -= NE2000_PMEM_SIZE;
                if (index + s->tcnt <= NE2000_PMEM_END) {
                    qemu_send_packet(qemu_get_queue(s->nic), s->mem + index,
                                     s->tcnt);
                }
                s->tsr = ENTSR_PTX;
                s->isr |= ENISR_TX;
                s->cmd &= ~E8390_TRANS;
                ne2000_update_irq(s);
            }
        }
    } else {
        page = s->cmd >> 6;
        offset = addr | (page << 4);
        switch(offset) {
        case EN0_STARTPG:
            if (val << 8 <= NE2000_PMEM_END) {
                s->start = val << 8;
            }
            break;
        case EN0_STOPPG:
            if (val << 8 <= NE2000_PMEM_END) {
                s->stop = val << 8;
            }
            break;
        case EN0_BOUNDARY:
            if (val << 8 < NE2000_PMEM_END) {
                s->boundary = val;
            }
            break;
        case EN0_IMR:
            s->imr = val;
            ne2000_update_irq(s);
            break;
        case EN0_TPSR:
            s->tpsr = val;
            break;
        case EN0_TCNTLO:
            s->tcnt = (s->tcnt & 0xff00) | val;
            break;
        case EN0_TCNTHI:
            s->tcnt = (s->tcnt & 0x00ff) | (val << 8);
            break;
        case EN0_RSARLO:
            s->rsar = (s->rsar & 0xff00) | val;
            break;
        case EN0_RSARHI:
            s->rsar = (s->rsar & 0x00ff) | (val << 8);
            break;
        case EN0_RCNTLO:
            s->rcnt = (s->rcnt & 0xff00) | val;
            break;
        case EN0_RCNTHI:
            s->rcnt = (s->rcnt & 0x00ff) | (val << 8);
            break;
        case EN0_RXCR:
            s->rxcr = val;
            break;
        case EN0_DCFG:
            s->dcfg = val;
            break;
        case EN0_ISR:
            s->isr &= ~(val & 0x7f);
            ne2000_update_irq(s);
            break;
        case EN1_PHYS ... EN1_PHYS + 5:
            s->phys[offset - EN1_PHYS] = val;
            break;
        case EN1_CURPAG:
            if (val << 8 < NE2000_PMEM_END) {
                s->curpag = val;
            }
            break;
        case EN1_MULT ... EN1_MULT + 7:
            s->mult[offset - EN1_MULT] = val;
            break;
        }
    }
}

static uint32_t ne2000_ioport_read(void *opaque, uint32_t addr)
{
    NE2000State *s = opaque;
    int offset, page, ret;

    addr &= 0xf;
    if (addr == E8390_CMD) {
        ret = s->cmd;
    } else {
        page = s->cmd >> 6;
        offset = addr | (page << 4);
        switch(offset) {
        case EN0_TSR:
            ret = s->tsr;
            break;
        case EN0_BOUNDARY:
            ret = s->boundary;
            break;
        case EN0_ISR:
            ret = s->isr;
            break;
        case EN0_RSARLO:
            ret = s->rsar & 0x00ff;
            break;
        case EN0_RSARHI:
            ret = s->rsar >> 8;
            break;
        case EN1_PHYS ... EN1_PHYS + 5:
            ret = s->phys[offset - EN1_PHYS];
            break;
        case EN1_CURPAG:
            ret = s->curpag;
            break;
        case EN1_MULT ... EN1_MULT + 7:
            ret = s->mult[offset - EN1_MULT];
            break;
        case EN0_RSR:
            ret = s->rsr;
            break;
        case EN2_STARTPG:
            ret = s->start >> 8;
            break;
        case EN2_STOPPG:
            ret = s->stop >> 8;
            break;
        case EN0_RTL8029ID0:
            ret = 0x50;
            break;
        case EN0_RTL8029ID1:
            ret = 0x43;
            break;
        case EN3_CONFIG0:
            ret = 0;
            break;
        case EN3_CONFIG2:
            ret = 0x40;
            break;
        case EN3_CONFIG3:
            ret = 0x40;
            break;
        default:
            ret = 0x00;
            break;
        }
    }
    trace_ne2000_ioport_read(addr, ret);
    return ret;
}

static inline void ne2000_mem_writeb(NE2000State *s, uint32_t addr,
                                     uint32_t val)
{
    if (addr < 32 ||
        (addr >= NE2000_PMEM_START && addr < NE2000_MEM_SIZE)) {
        s->mem[addr] = val;
    }
}

static inline void ne2000_mem_writew(NE2000State *s, uint32_t addr,
                                     uint32_t val)
{
    addr &= ~1;
    if (addr < 32 ||
        (addr >= NE2000_PMEM_START && addr < NE2000_MEM_SIZE)) {
        *(uint16_t *)(s->mem + addr) = cpu_to_le16(val);
    }
}

static inline void ne2000_mem_writel(NE2000State *s, uint32_t addr,
                                     uint32_t val)
{
    addr &= ~1;
    if (addr < 32
        || (addr >= NE2000_PMEM_START
            && addr + sizeof(uint32_t) <= NE2000_MEM_SIZE)) {
        stl_le_p(s->mem + addr, val);
    }
}

static inline uint32_t ne2000_mem_readb(NE2000State *s, uint32_t addr)
{
    if (addr < 32 ||
        (addr >= NE2000_PMEM_START && addr < NE2000_MEM_SIZE)) {
        return s->mem[addr];
    } else {
        return 0xff;
    }
}

static inline uint32_t ne2000_mem_readw(NE2000State *s, uint32_t addr)
{
    addr &= ~1;
    if (addr < 32 ||
        (addr >= NE2000_PMEM_START && addr < NE2000_MEM_SIZE)) {
        return le16_to_cpu(*(uint16_t *)(s->mem + addr));
    } else {
        return 0xffff;
    }
}

static inline uint32_t ne2000_mem_readl(NE2000State *s, uint32_t addr)
{
    addr &= ~1;
    if (addr < 32
        || (addr >= NE2000_PMEM_START
            && addr + sizeof(uint32_t) <= NE2000_MEM_SIZE)) {
        return ldl_le_p(s->mem + addr);
    } else {
        return 0xffffffff;
    }
}

static inline void ne2000_dma_update(NE2000State *s, int len)
{
    s->rsar += len;
    if (s->rsar == s->stop)
        s->rsar = s->start;

    if (s->rcnt <= len) {
        s->rcnt = 0;
        s->isr |= ENISR_RDC;
        ne2000_update_irq(s);
    } else {
        s->rcnt -= len;
    }
}

static void ne2000_asic_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    NE2000State *s = opaque;

#ifdef DEBUG_NE2000
    printf("NE2000: asic write val=0x%04x\n", val);
#endif
    if (s->rcnt == 0)
        return;
    if (s->dcfg & 0x01) {
        ne2000_mem_writew(s, s->rsar, val);
        ne2000_dma_update(s, 2);
    } else {
        ne2000_mem_writeb(s, s->rsar, val);
        ne2000_dma_update(s, 1);
    }
}

static uint32_t ne2000_asic_ioport_read(void *opaque, uint32_t addr)
{
    NE2000State *s = opaque;
    int ret;

    if (s->dcfg & 0x01) {
        ret = ne2000_mem_readw(s, s->rsar);
        ne2000_dma_update(s, 2);
    } else {
        ret = ne2000_mem_readb(s, s->rsar);
        ne2000_dma_update(s, 1);
    }
#ifdef DEBUG_NE2000
    printf("NE2000: asic read val=0x%04x\n", ret);
#endif
    return ret;
}

static void ne2000_asic_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    NE2000State *s = opaque;

#ifdef DEBUG_NE2000
    printf("NE2000: asic writel val=0x%04x\n", val);
#endif
    if (s->rcnt == 0)
        return;
    ne2000_mem_writel(s, s->rsar, val);
    ne2000_dma_update(s, 4);
}

static uint32_t ne2000_asic_ioport_readl(void *opaque, uint32_t addr)
{
    NE2000State *s = opaque;
    int ret;

    ret = ne2000_mem_readl(s, s->rsar);
    ne2000_dma_update(s, 4);
#ifdef DEBUG_NE2000
    printf("NE2000: asic readl val=0x%04x\n", ret);
#endif
    return ret;
}

static void ne2000_reset_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
}

static uint32_t ne2000_reset_ioport_read(void *opaque, uint32_t addr)
{
    NE2000State *s = opaque;
    ne2000_reset(s);
    return 0;
}

static int ne2000_post_load(void* opaque, int version_id)
{
    NE2000State* s = opaque;

    if (version_id < 2) {
        s->rxcr = 0x0c;
    }
    return 0;
}

const VMStateDescription vmstate_ne2000 = {
    .name = "ne2000",
    .version_id = 2,
    .minimum_version_id = 0,
    .post_load = ne2000_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_V(rxcr, NE2000State, 2),
        VMSTATE_UINT8(cmd, NE2000State),
        VMSTATE_UINT32(start, NE2000State),
        VMSTATE_UINT32(stop, NE2000State),
        VMSTATE_UINT8(boundary, NE2000State),
        VMSTATE_UINT8(tsr, NE2000State),
        VMSTATE_UINT8(tpsr, NE2000State),
        VMSTATE_UINT16(tcnt, NE2000State),
        VMSTATE_UINT16(rcnt, NE2000State),
        VMSTATE_UINT32(rsar, NE2000State),
        VMSTATE_UINT8(rsr, NE2000State),
        VMSTATE_UINT8(isr, NE2000State),
        VMSTATE_UINT8(dcfg, NE2000State),
        VMSTATE_UINT8(imr, NE2000State),
        VMSTATE_BUFFER(phys, NE2000State),
        VMSTATE_UINT8(curpag, NE2000State),
        VMSTATE_BUFFER(mult, NE2000State),
        VMSTATE_UNUSED(4),
        VMSTATE_BUFFER(mem, NE2000State),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t ne2000_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    NE2000State *s = opaque;
    uint64_t val;

    if (addr < 0x10 && size == 1) {
        val = ne2000_ioport_read(s, addr);
    } else if (addr == 0x10) {
        if (size <= 2) {
            val = ne2000_asic_ioport_read(s, addr);
        } else {
            val = ne2000_asic_ioport_readl(s, addr);
        }
    } else if (addr == 0x1f && size == 1) {
        val = ne2000_reset_ioport_read(s, addr);
    } else {
        val = ((uint64_t)1 << (size * 8)) - 1;
    }
    trace_ne2000_read(addr, val);

    return val;
}

static void ne2000_write(void *opaque, hwaddr addr,
                         uint64_t data, unsigned size)
{
    NE2000State *s = opaque;

    trace_ne2000_write(addr, data);
    if (addr < 0x10 && size == 1) {
        ne2000_ioport_write(s, addr, data);
    } else if (addr == 0x10) {
        if (size <= 2) {
            ne2000_asic_ioport_write(s, addr, data);
        } else {
            ne2000_asic_ioport_writel(s, addr, data);
        }
    } else if (addr == 0x1f && size == 1) {
        ne2000_reset_ioport_write(s, addr, data);
    }
}

static const MemoryRegionOps ne2000_ops = {
    .read = ne2000_read,
    .write = ne2000_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void ne2000_setup_io(NE2000State *s, DeviceState *dev, unsigned size)
{
    memory_region_init_io(&s->io, OBJECT(dev), &ne2000_ops, s, "ne2000", size);
}
