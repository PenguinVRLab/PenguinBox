/*
 * TI OMAP on-chip I2C controller.  Only "new I2C" mode supported.
 *
 * Copyright (C) 2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/arm/omap.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

struct OMAPI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq drq[2];
    I2CBus *bus;

    uint8_t revision;
    void *iclk;
    void *fclk;

    uint8_t mask;
    uint16_t stat;
    uint16_t dma;
    uint16_t count;
    int count_cur;
    uint32_t fifo;
    int rxlen;
    int txlen;
    uint16_t control;
    uint16_t addr[2];
    uint8_t divider;
    uint8_t times[2];
    uint16_t test;
};

#define OMAP2_INTR_REV  0x34
#define OMAP2_GC_REV    0x34

static void omap_i2c_interrupts_update(OMAPI2CState *s)
{
    qemu_set_irq(s->irq, s->stat & s->mask);
    if ((s->dma >> 15) & 1)
        qemu_set_irq(s->drq[0], (s->stat >> 3) & 1);
    if ((s->dma >> 7) & 1)
        qemu_set_irq(s->drq[1], (s->stat >> 4) & 1);
}

static void omap_i2c_fifo_run(OMAPI2CState *s)
{
    int ack = 1;

    if (!i2c_bus_busy(s->bus))
        return;

    if ((s->control >> 2) & 1) {
        if ((s->control >> 1) & 1) {
            i2c_end_transfer(s->bus);
            s->control &= ~(1 << 1);
            s->count_cur = s->count;
            s->txlen = 0;
        } else if ((s->control >> 9) & 1) {
            while (ack && s->txlen)
                ack = (i2c_send(s->bus,
                                        (s->fifo >> ((-- s->txlen) << 3)) &
                                        0xff) >= 0);
            s->stat |= 1 << 4;
        } else {
            while (s->rxlen < 4)
                s->fifo |= i2c_recv(s->bus) << ((s->rxlen ++) << 3);
            s->stat |= 1 << 3;
        }
    } else {
        if ((s->control >> 9) & 1) {
            while (ack && s->count_cur && s->txlen) {
                ack = (i2c_send(s->bus,
                                        (s->fifo >> ((-- s->txlen) << 3)) &
                                        0xff) >= 0);
                s->count_cur --;
            }
            if (ack && s->count_cur)
                s->stat |= 1 << 4;
            else
                s->stat &= ~(1 << 4);
            if (!s->count_cur) {
                s->stat |= 1 << 2;
                s->control &= ~(1 << 10);
            }
        } else {
            while (s->count_cur && s->rxlen < 4) {
                s->fifo |= i2c_recv(s->bus) << ((s->rxlen ++) << 3);
                s->count_cur --;
            }
            if (s->rxlen)
                s->stat |= 1 << 3;
            else
                s->stat &= ~(1 << 3);
        }
        if (!s->count_cur) {
            if ((s->control >> 1) & 1) {
                i2c_end_transfer(s->bus);
                s->control &= ~(1 << 1);
                s->count_cur = s->count;
                s->txlen = 0;
            } else {
                s->stat |= 1 << 2;
                s->control &= ~(1 << 10);
            }
        }
    }

    s->stat |= (!ack) << 1;
    if (!ack)
        s->control &= ~(1 << 1);
}

static void omap_i2c_reset(DeviceState *dev)
{
    OMAPI2CState *s = OMAP_I2C(dev);

    s->mask = 0;
    s->stat = 0;
    s->dma = 0;
    s->count = 0;
    s->count_cur = 0;
    s->fifo = 0;
    s->rxlen = 0;
    s->txlen = 0;
    s->control = 0;
    s->addr[0] = 0;
    s->addr[1] = 0;
    s->divider = 0;
    s->times[0] = 0;
    s->times[1] = 0;
    s->test = 0;
}

static uint32_t omap_i2c_read(void *opaque, hwaddr addr)
{
    OMAPI2CState *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    switch (offset) {
    case 0x00:
        return s->revision;

    case 0x04:
        return s->mask;

    case 0x08:
        return s->stat | (i2c_bus_busy(s->bus) << 12);

    case 0x0c:
        if (s->revision >= OMAP2_INTR_REV)
            break;
        ret = ctz32(s->stat & s->mask);
        if (ret != 32) {
            s->stat ^= 1 << ret;
            ret++;
        } else {
            ret = 0;
        }
        omap_i2c_interrupts_update(s);
        return ret;

    case 0x10:
        return (s->control >> 15) & 1;

    case 0x14:
        return s->dma;

    case 0x18:
        return s->count_cur;

    case 0x1c:
        ret = 0;
        if (s->control & (1 << 14)) {
            ret |= ((s->fifo >> 0) & 0xff) << 8;
            ret |= ((s->fifo >> 8) & 0xff) << 0;
        } else {
            ret |= ((s->fifo >> 8) & 0xff) << 8;
            ret |= ((s->fifo >> 0) & 0xff) << 0;
        }
        if (s->rxlen == 1) {
            s->stat |= 1 << 15;
            s->rxlen = 0;
        } else if (s->rxlen > 1) {
            if (s->rxlen > 2)
                s->fifo >>= 16;
            s->rxlen -= 2;
        } else {
        }
        if (!s->rxlen) {
            s->stat &= ~(1 << 3);
            if (((s->control >> 10) & 1) &&
                            ((~s->control >> 9) & 1)) {
                s->stat |= 1 << 2;
                s->control &= ~(1 << 10);
            }
        }
        s->stat &= ~(1 << 11);
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        return ret;

    case 0x20:
        return 0;

    case 0x24:
        return s->control;

    case 0x28:
        return s->addr[0];

    case 0x2c:
        return s->addr[1];

    case 0x30:
        return s->divider;

    case 0x34:
        return s->times[0];

    case 0x38:
        return s->times[1];

    case 0x3c:
        if (s->test & (1 << 15)) {
            s->test ^= 0xa;
            return s->test;
        } else
            return s->test & ~0x300f;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_i2c_write(void *opaque, hwaddr addr,
                uint32_t value)
{
    OMAPI2CState *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    int nack;

    switch (offset) {
    case 0x00:
    case 0x0c:
    case 0x10:
        OMAP_RO_REG(addr);
        return;

    case 0x04:
        s->mask = value & (s->revision < OMAP2_GC_REV ? 0x1f : 0x3f);
        break;

    case 0x08:
        if (s->revision < OMAP2_INTR_REV) {
            OMAP_RO_REG(addr);
            return;
        }

        s->stat &= ~(value & 0x27);
        omap_i2c_interrupts_update(s);
        break;

    case 0x14:
        s->dma = value & 0x8080;
        if (value & (1 << 15))
            s->mask &= ~(1 << 3);
        if (value & (1 << 7))
            s->mask &= ~(1 << 4);
        break;

    case 0x18:
        s->count = value;
        break;

    case 0x1c:
        if (s->txlen > 2) {
            break;
        }
        s->fifo <<= 16;
        s->txlen += 2;
        if (s->control & (1 << 14)) {
            s->fifo |= ((value >> 8) & 0xff) << 8;
            s->fifo |= ((value >> 0) & 0xff) << 0;
        } else {
            s->fifo |= ((value >> 0) & 0xff) << 8;
            s->fifo |= ((value >> 8) & 0xff) << 0;
        }
        s->stat &= ~(1 << 10);
        if (s->txlen > 2)
            s->stat &= ~(1 << 4);
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        break;

    case 0x20:
        if (s->revision < OMAP2_INTR_REV) {
            OMAP_BAD_REG(addr);
            return;
        }

        if (value & 2) {
            omap_i2c_reset(DEVICE(s));
        }
        break;

    case 0x24:
        s->control = value & 0xcf87;
        if (~value & (1 << 15)) {
            if (s->revision < OMAP2_INTR_REV) {
                omap_i2c_reset(DEVICE(s));
            }
            break;
        }
        if ((value & (1 << 15)) && !(value & (1 << 10))) {
            qemu_log_mask(LOG_UNIMP, "%s: I^2C slave mode not supported\n",
                          __func__);
            break;
        }
        if ((value & (1 << 15)) && value & (1 << 8)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: 10-bit addressing mode not supported\n",
                          __func__);
            break;
        }
        if ((value & (1 << 15)) && value & (1 << 0)) {
            nack = !!i2c_start_transfer(s->bus, s->addr[1],
                            (~value >> 9) & 1);
            s->stat |= nack << 1;
            s->control &= ~(1 << 0);
            s->fifo = 0;
            if (nack)
                s->control &= ~(1 << 1);
            else {
                s->count_cur = s->count;
                omap_i2c_fifo_run(s);
            }
            omap_i2c_interrupts_update(s);
        }
        break;

    case 0x28:
        s->addr[0] = value & 0x3ff;
        break;

    case 0x2c:
        s->addr[1] = value & 0x3ff;
        break;

    case 0x30:
        s->divider = value;
        break;

    case 0x34:
        s->times[0] = value;
        break;

    case 0x38:
        s->times[1] = value;
        break;

    case 0x3c:
        s->test = value & 0xf80f;
        if (value & (1 << 11))
            if (s->revision >= OMAP2_INTR_REV) {
                s->stat |= 0x3f;
                omap_i2c_interrupts_update(s);
            }
        if (value & (1 << 15)) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: System Test not supported\n", __func__);
        }
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static void omap_i2c_writeb(void *opaque, hwaddr addr,
                uint32_t value)
{
    OMAPI2CState *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x1c:
        if (s->txlen > 2) {
            break;
        }
        s->fifo <<= 8;
        s->txlen += 1;
        s->fifo |= value & 0xff;
        s->stat &= ~(1 << 10);
        if (s->txlen > 2)
            s->stat &= ~(1 << 4);
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static uint64_t omap_i2c_readfn(void *opaque, hwaddr addr,
                                unsigned size)
{
    switch (size) {
    case 2:
        return omap_i2c_read(opaque, addr);
    default:
        return omap_badwidth_read16(opaque, addr);
    }
}

static void omap_i2c_writefn(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        omap_i2c_writeb(opaque, addr, value);
        break;
    case 2:
        omap_i2c_write(opaque, addr, value);
        break;
    default:
        omap_badwidth_write16(opaque, addr, value);
        break;
    }
}

static const MemoryRegionOps omap_i2c_ops = {
    .read = omap_i2c_readfn,
    .write = omap_i2c_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    OMAPI2CState *s = OMAP_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->drq[0]);
    sysbus_init_irq(sbd, &s->drq[1]);
    sysbus_init_mmio(sbd, &s->iomem);
    s->bus = i2c_init_bus(dev, NULL);
}

static void omap_i2c_realize(DeviceState *dev, Error **errp)
{
    OMAPI2CState *s = OMAP_I2C(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &omap_i2c_ops, s, "omap.i2c",
                          (s->revision < OMAP2_INTR_REV) ? 0x800 : 0x1000);

    if (!s->fclk) {
        error_setg(errp, "omap_i2c: fclk not connected");
        return;
    }
    if (s->revision >= OMAP2_INTR_REV && !s->iclk) {
        error_setg(errp, "omap_i2c: iclk not connected");
        return;
    }
}

void omap_i2c_set_iclk(OMAPI2CState *i2c, omap_clk clk)
{
    i2c->iclk = clk;
}

void omap_i2c_set_fclk(OMAPI2CState *i2c, omap_clk clk)
{
    i2c->fclk = clk;
}

static const Property omap_i2c_properties[] = {
    DEFINE_PROP_UINT8("revision", OMAPI2CState, revision, 0),
};

static void omap_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, omap_i2c_properties);
    device_class_set_legacy_reset(dc, omap_i2c_reset);
    dc->user_creatable = false;
    dc->realize = omap_i2c_realize;
}

static const TypeInfo omap_i2c_info = {
    .name = TYPE_OMAP_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OMAPI2CState),
    .instance_init = omap_i2c_init,
    .class_init = omap_i2c_class_init,
};

static void omap_i2c_register_types(void)
{
    type_register_static(&omap_i2c_info);
}

I2CBus *omap_i2c_bus(DeviceState *omap_i2c)
{
    OMAPI2CState *s = OMAP_I2C(omap_i2c);
    return s->bus;
}

type_init(omap_i2c_register_types)
