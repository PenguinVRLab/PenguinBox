/*
 * IMX31 UARTS
 *
 * Copyright (c) 2008 OKL
 * Originally Written by Hans Jiang
 * Copyright (c) 2011 NICTA Pty Ltd.
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is a `bare-bones' implementation of the IMX series serial ports.
 * TODO:
 *  -- implement FIFOs.  The real hardware has 32 word transmit
 *                       and receive FIFOs; we currently use a 1-char buffer
 *  -- implement DMA
 *  -- implement BAUD-rate and modem lines, for when the backend
 *     is a real serial device.
 */

#include "qemu/osdep.h"
#include "hw/char/imx_serial.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/fifo32.h"
#include "trace.h"

#ifndef DEBUG_IMX_UART
#define DEBUG_IMX_UART 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_UART) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_SERIAL, \
                                             __func__, ##args); \
        } \
    } while (0)

static const VMStateDescription vmstate_imx_serial = {
    .name = TYPE_IMX_SERIAL,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO32(rx_fifo, IMXSerialState),
        VMSTATE_TIMER(ageing_timer, IMXSerialState),
        VMSTATE_UINT32(usr1, IMXSerialState),
        VMSTATE_UINT32(usr2, IMXSerialState),
        VMSTATE_UINT32(ucr1, IMXSerialState),
        VMSTATE_UINT32(uts1, IMXSerialState),
        VMSTATE_UINT32(onems, IMXSerialState),
        VMSTATE_UINT32(ufcr, IMXSerialState),
        VMSTATE_UINT32(ubmr, IMXSerialState),
        VMSTATE_UINT32(ubrc, IMXSerialState),
        VMSTATE_UINT32(ucr3, IMXSerialState),
        VMSTATE_UINT32(ucr4, IMXSerialState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx_update(IMXSerialState *s)
{
    uint32_t usr1;
    uint32_t usr2;
    uint32_t mask;

    usr1 = s->usr1 & s->ucr1 & (USR1_TRDY | USR1_RRDY);
    usr1 |= (s->ucr2 & UCR2_ATEN) ? (s->usr1 & USR1_AGTIM) : 0;
    mask = (s->ucr1 & UCR1_TXMPTYEN) ? USR2_TXFE : 0;
    mask |= s->ucr4 & (UCR4_WKEN | UCR4_TCEN | UCR4_DREN | UCR4_OREN);

    usr2 = s->usr2 & mask;

    qemu_set_irq(s->irq, usr1 || usr2);
}

static void imx_serial_rx_fifo_push(IMXSerialState *s, uint32_t value)
{
    uint32_t pushed_value = value;
    if (fifo32_is_full(&s->rx_fifo)) {
        s->usr2 |= USR2_ORE;
    } else {
        if (fifo32_num_used(&s->rx_fifo) == FIFO_SIZE - 1) {
            pushed_value |= URXD_ERR | URXD_OVRRUN;
        }
        fifo32_push(&s->rx_fifo, pushed_value);
    }
}

static uint32_t imx_serial_rx_fifo_pop(IMXSerialState *s)
{
    if (fifo32_is_empty(&s->rx_fifo)) {
        return 0;
    }
    return fifo32_pop(&s->rx_fifo);
}

static void imx_serial_rx_fifo_ageing_timer_int(void *opaque)
{
    IMXSerialState *s = (IMXSerialState *) opaque;
    s->usr1 |= USR1_AGTIM;
    imx_update(s);
}

static void imx_serial_rx_fifo_ageing_timer_restart(void *opaque)
{
    IMXSerialState *s = (IMXSerialState *) opaque;

    if (!(s->usr1 & USR1_RRDY) && !(s->uts1 & UTS1_RXEMPTY)) {
        timer_mod_ns(&s->ageing_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + AGE_DURATION_NS);
    } else {
        timer_del(&s->ageing_timer);
    }
}

static void imx_serial_reset(IMXSerialState *s)
{

    s->usr1 = USR1_TRDY | USR1_RXDS;
    s->usr1 |= USR1_RTSS;
    s->usr2 = USR2_TXFE | USR2_TXDC | USR2_DCDIN;
    s->uts1 = UTS1_RXEMPTY | UTS1_TXEMPTY;
    s->ucr1 = 0;
    s->ucr2 = UCR2_SRST;
    s->ucr3 = 0x700;
    s->ubmr = 0;
    s->ubrc = 4;
    s->ufcr = BIT(11) | BIT(0);

    fifo32_reset(&s->rx_fifo);
    timer_del(&s->ageing_timer);
}

static void imx_serial_reset_at_boot(DeviceState *dev)
{
    IMXSerialState *s = IMX_SERIAL(dev);

    imx_serial_reset(s);

    /*
     * enable the uart on boot, so messages from the linux decompressor
     * are visible.  On real hardware this is done by the boot rom
     * before anything else is loaded.
     */
    s->ucr1 = UCR1_UARTEN;
    s->ucr2 = UCR2_TXEN;

}

static uint64_t imx_serial_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);
    uint32_t c, rx_used;
    uint8_t rxtl = s->ufcr & TL_MASK;
    uint64_t value;

    switch (offset >> 2) {
    case 0x0:
        c = imx_serial_rx_fifo_pop(s);
        if (!(s->uts1 & UTS1_RXEMPTY)) {
            c |= URXD_CHARRDY;
            rx_used = fifo32_num_used(&s->rx_fifo);
            if (rx_used < rxtl) {
                s->usr1 &= ~USR1_RRDY;
            }
            if (rx_used == 0) {
                s->usr2 &= ~USR2_RDR;
                s->uts1 |= UTS1_RXEMPTY;
            }
            imx_update(s);
            imx_serial_rx_fifo_ageing_timer_restart(s);
            qemu_chr_fe_accept_input(&s->chr);
        }
        value = c;
        break;

    case 0x20:
        value = s->ucr1;
        break;

    case 0x21:
        value = s->ucr2;
        break;

    case 0x25:
        value = s->usr1;
        break;

    case 0x26:
        value = s->usr2;
        break;

    case 0x2A:
        value = s->ubmr;
        break;

    case 0x2B:
        value = s->ubrc;
        break;

    case 0x2d:
        value = s->uts1;
        break;

    case 0x24:
        value = s->ufcr;
        break;

    case 0x2c:
        value = s->onems;
        break;

    case 0x22:
        value = s->ucr3;
        break;

    case 0x23:
        value = s->ucr4;
        break;

    case 0x29:
        value = 0x0;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SERIAL, __func__, offset);
        value = 0;
        break;
    }

    trace_imx_serial_read(chr ? chr->label : "NODEV", offset, value);

    return value;
}

static void imx_serial_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);
    unsigned char ch;

    trace_imx_serial_write(chr ? chr->label : "NODEV", offset, value);

    switch (offset >> 2) {
    case 0x10:
        ch = value;
        if (s->ucr2 & UCR2_TXEN) {
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->usr1 &= ~USR1_TRDY;
            s->usr2 &= ~USR2_TXDC;
            imx_update(s);
            s->usr1 |= USR1_TRDY;
            s->usr2 |= USR2_TXDC;
            imx_update(s);
        }
        break;

    case 0x20:
        s->ucr1 = value & 0xffff;

        DPRINTF("write(ucr1=%x)\n", (unsigned int)value);

        imx_update(s);
        break;

    case 0x21:
        if (!(value & UCR2_SRST)) {
            imx_serial_reset(s);
            imx_update(s);
            value |= UCR2_SRST;
        }
        if (value & UCR2_RXEN) {
            if (!(s->ucr2 & UCR2_RXEN)) {
                qemu_chr_fe_accept_input(&s->chr);
            }
        }
        s->ucr2 = value & 0xffff;
        break;

    case 0x25:
        value &= USR1_AWAKE | USR1_AIRINT | USR1_DTRD | USR1_AGTIM |
                 USR1_FRAMERR | USR1_ESCF | USR1_RTSD | USR1_PARTYER;
        s->usr1 &= ~value;
        break;

    case 0x26:
        value &= USR2_ADET | USR2_DTRF | USR2_IDLE | USR2_ACST |
                 USR2_RIDELT | USR2_IRINT | USR2_WAKE |
                 USR2_DCDDELT | USR2_RTSF | USR2_BRCD | USR2_ORE;
        s->usr2 &= ~value;
        break;

    case 0x29:
        s->ubrc = value & 0xffff;
        break;

    case 0x2a:
        s->ubmr = value & 0xffff;
        break;

    case 0x2c:
        s->onems = value & 0xffff;
        break;

    case 0x24:
        s->ufcr = value & 0xffff;
        break;

    case 0x22:
        s->ucr3 = value & 0xffff;
        break;

    case 0x23:
        s->ucr4 = value & 0xffff;
        imx_update(s);
        break;

    case 0x2d:
        qemu_log_mask(LOG_UNIMP, "[%s]%s: Unimplemented reg 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SERIAL, __func__, offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SERIAL, __func__, offset);
    }
}

static int imx_can_receive(void *opaque)
{
    IMXSerialState *s = (IMXSerialState *)opaque;

    return s->ucr2 & UCR2_RXEN ? fifo32_num_free(&s->rx_fifo) : 0;
}

static void imx_put_data(void *opaque, uint32_t value)
{
    IMXSerialState *s = (IMXSerialState *)opaque;
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);
    uint8_t rxtl = s->ufcr & TL_MASK;

    trace_imx_serial_put_data(chr ? chr->label : "NODEV", value);

    imx_serial_rx_fifo_push(s, value);
    if (fifo32_num_used(&s->rx_fifo) >= rxtl) {
        s->usr1 |= USR1_RRDY;
    }
    s->usr2 |= USR2_RDR;
    s->uts1 &= ~UTS1_RXEMPTY;
    if (value & URXD_BRK) {
        s->usr2 |= USR2_BRCD;
    }

    imx_serial_rx_fifo_ageing_timer_restart(s);

    imx_update(s);
}

static void imx_receive(void *opaque, const uint8_t *buf, int size)
{
    IMXSerialState *s = (IMXSerialState *)opaque;

    s->usr2 |= USR2_WAKE;

    for (int i = 0; i < size; i++) {
        imx_put_data(opaque, buf[i]);
    }
}

static void imx_event(void *opaque, QEMUChrEvent event)
{
    if (event == CHR_EVENT_BREAK) {
        imx_put_data(opaque, URXD_BRK | URXD_FRMERR | URXD_ERR);
    }
}


static const struct MemoryRegionOps imx_serial_ops = {
    .read = imx_serial_read,
    .write = imx_serial_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void imx_serial_realize(DeviceState *dev, Error **errp)
{
    IMXSerialState *s = IMX_SERIAL(dev);

    fifo32_create(&s->rx_fifo, FIFO_SIZE);
    timer_init_ns(&s->ageing_timer, QEMU_CLOCK_VIRTUAL,
                  imx_serial_rx_fifo_ageing_timer_int, s);

    DPRINTF("char dev for uart: %p\n", qemu_chr_fe_get_driver(&s->chr));

    qemu_chr_fe_set_handlers(&s->chr, imx_can_receive, imx_receive,
                             imx_event, NULL, s, NULL, true);
}

static void imx_serial_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMXSerialState *s = IMX_SERIAL(obj);

    memory_region_init_io(&s->iomem, obj, &imx_serial_ops, s,
                          TYPE_IMX_SERIAL, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const Property imx_serial_properties[] = {
    DEFINE_PROP_CHR("chardev", IMXSerialState, chr),
};

static void imx_serial_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_serial_realize;
    dc->vmsd = &vmstate_imx_serial;
    device_class_set_legacy_reset(dc, imx_serial_reset_at_boot);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "i.MX series UART";
    device_class_set_props(dc, imx_serial_properties);
}

static const TypeInfo imx_serial_info = {
    .name           = TYPE_IMX_SERIAL,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMXSerialState),
    .instance_init  = imx_serial_init,
    .class_init     = imx_serial_class_init,
};

static void imx_serial_register_types(void)
{
    type_register_static(&imx_serial_info);
}

type_init(imx_serial_register_types)
