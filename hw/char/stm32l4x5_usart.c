/*
 * STM32L4X5 USART (Universal Synchronous Asynchronous Receiver Transmitter)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The STM32L4X5 USART is heavily inspired by the stm32f2xx_usart
 * by Alistair Francis.
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "migration/vmstate.h"
#include "hw/char/stm32l4x5_usart.h"
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "trace.h"


REG32(CR1, 0x00)
    FIELD(CR1, M1, 28, 1)
    FIELD(CR1, EOBIE, 27, 1)
    FIELD(CR1, RTOIE, 26, 1)
    FIELD(CR1, DEAT, 21, 5)
    FIELD(CR1, DEDT, 16, 5)
    FIELD(CR1, OVER8, 15, 1)
    FIELD(CR1, CMIE, 14, 1)
    FIELD(CR1, MME, 13, 1)
    FIELD(CR1, M0, 12, 1)
    FIELD(CR1, WAKE, 11, 1)
    FIELD(CR1, PCE, 10, 1)
    FIELD(CR1, PS, 9, 1)
    FIELD(CR1, PEIE, 8, 1)
    FIELD(CR1, TXEIE, 7, 1)
    FIELD(CR1, TCIE, 6, 1)
    FIELD(CR1, RXNEIE, 5, 1)
    FIELD(CR1, IDLEIE, 4, 1)
    FIELD(CR1, TE, 3, 1)
    FIELD(CR1, RE, 2, 1)
    FIELD(CR1, UESM, 1, 1)
    FIELD(CR1, UE, 0, 1)
REG32(CR2, 0x04)
    FIELD(CR2, ADD_1, 28, 4)
    FIELD(CR2, ADD_0, 24, 4)
    FIELD(CR2, RTOEN, 23, 1)
    FIELD(CR2, ABRMOD, 21, 2)
    FIELD(CR2, ABREN, 20, 1)
    FIELD(CR2, MSBFIRST, 19, 1)
    FIELD(CR2, DATAINV, 18, 1)
    FIELD(CR2, TXINV, 17, 1)
    FIELD(CR2, RXINV, 16, 1)
    FIELD(CR2, SWAP, 15, 1)
    FIELD(CR2, LINEN, 14, 1)
    FIELD(CR2, STOP, 12, 2)
    FIELD(CR2, CLKEN, 11, 1)
    FIELD(CR2, CPOL, 10, 1)
    FIELD(CR2, CPHA, 9, 1)
    FIELD(CR2, LBCL, 8, 1)
    FIELD(CR2, LBDIE, 6, 1)
    FIELD(CR2, LBDL, 5, 1)
    FIELD(CR2, ADDM7, 4, 1)

REG32(CR3, 0x08)
    FIELD(CR3, UCESM, 23, 1)
    FIELD(CR3, WUFIE, 22, 1)
    FIELD(CR3, WUS, 20, 2)
    FIELD(CR3, SCARCNT, 17, 3)
    FIELD(CR3, DEP, 15, 1)
    FIELD(CR3, DEM, 14, 1)
    FIELD(CR3, DDRE, 13, 1)
    FIELD(CR3, OVRDIS, 12, 1)
    FIELD(CR3, ONEBIT, 11, 1)
    FIELD(CR3, CTSIE, 10, 1)
    FIELD(CR3, CTSE, 9, 1)
    FIELD(CR3, RTSE, 8, 1)
    FIELD(CR3, DMAT, 7, 1)
    FIELD(CR3, DMAR, 6, 1)
    FIELD(CR3, SCEN, 5, 1)
    FIELD(CR3, NACK, 4, 1)
    FIELD(CR3, HDSEL, 3, 1)
    FIELD(CR3, IRLP, 2, 1)
    FIELD(CR3, IREN, 1, 1)
    FIELD(CR3, EIE, 0, 1)
REG32(BRR, 0x0C)
    FIELD(BRR, BRR, 0, 16)
REG32(GTPR, 0x10)
    FIELD(GTPR, GT, 8, 8)
    FIELD(GTPR, PSC, 0, 8)
REG32(RTOR, 0x14)
    FIELD(RTOR, BLEN, 24, 8)
    FIELD(RTOR, RTO, 0, 24)
REG32(RQR, 0x18)
    FIELD(RQR, TXFRQ, 4, 1)
    FIELD(RQR, RXFRQ, 3, 1)
    FIELD(RQR, MMRQ, 2, 1)
    FIELD(RQR, SBKRQ, 1, 1)
    FIELD(RQR, ABBRRQ, 0, 1)
REG32(ISR, 0x1C)
    FIELD(ISR, REACK, 22, 1)
    FIELD(ISR, TEACK, 21, 1)
    FIELD(ISR, WUF, 20, 1)
    FIELD(ISR, RWU, 19, 1)
    FIELD(ISR, SBKF, 18, 1)
    FIELD(ISR, CMF, 17, 1)
    FIELD(ISR, BUSY, 16, 1)
    FIELD(ISR, ABRF, 15, 1)
    FIELD(ISR, ABRE, 14, 1)
    FIELD(ISR, EOBF, 12, 1)
    FIELD(ISR, RTOF, 11, 1)
    FIELD(ISR, CTS, 10, 1)
    FIELD(ISR, CTSIF, 9, 1)
    FIELD(ISR, LBDF, 8, 1)
    FIELD(ISR, TXE, 7, 1)
    FIELD(ISR, TC, 6, 1)
    FIELD(ISR, RXNE, 5, 1)
    FIELD(ISR, IDLE, 4, 1)
    FIELD(ISR, ORE, 3, 1)
    FIELD(ISR, NF, 2, 1)
    FIELD(ISR, FE, 1, 1)
    FIELD(ISR, PE, 0, 1)
REG32(ICR, 0x20)
    FIELD(ICR, WUCF, 20, 1)
    FIELD(ICR, CMCF, 17, 1)
    FIELD(ICR, EOBCF, 12, 1)
    FIELD(ICR, RTOCF, 11, 1)
    FIELD(ICR, CTSCF, 9, 1)
    FIELD(ICR, LBDCF, 8, 1)
    FIELD(ICR, TCCF, 6, 1)
    FIELD(ICR, IDLECF, 4, 1)
    FIELD(ICR, ORECF, 3, 1)
    FIELD(ICR, NCF, 2, 1)
    FIELD(ICR, FECF, 1, 1)
    FIELD(ICR, PECF, 0, 1)
REG32(RDR, 0x24)
    FIELD(RDR, RDR, 0, 9)
REG32(TDR, 0x28)
    FIELD(TDR, TDR, 0, 9)

static void stm32l4x5_update_isr(Stm32l4x5UsartBaseState *s)
{
    if (s->cr1 & R_CR1_TE_MASK) {
        s->isr |= R_ISR_TEACK_MASK;
    } else {
        s->isr &= ~R_ISR_TEACK_MASK;
    }

    if (s->cr1 & R_CR1_RE_MASK) {
        s->isr |= R_ISR_REACK_MASK;
    } else {
        s->isr &= ~R_ISR_REACK_MASK;
    }
}

static void stm32l4x5_update_irq(Stm32l4x5UsartBaseState *s)
{
    if (((s->isr & R_ISR_WUF_MASK) && (s->cr3 & R_CR3_WUFIE_MASK))        ||
        ((s->isr & R_ISR_CMF_MASK) && (s->cr1 & R_CR1_CMIE_MASK))         ||
        ((s->isr & R_ISR_ABRF_MASK) && (s->cr1 & R_CR1_RXNEIE_MASK))      ||
        ((s->isr & R_ISR_EOBF_MASK) && (s->cr1 & R_CR1_EOBIE_MASK))       ||
        ((s->isr & R_ISR_RTOF_MASK) && (s->cr1 & R_CR1_RTOIE_MASK))       ||
        ((s->isr & R_ISR_CTSIF_MASK) && (s->cr3 & R_CR3_CTSIE_MASK))      ||
        ((s->isr & R_ISR_LBDF_MASK) && (s->cr2 & R_CR2_LBDIE_MASK))       ||
        ((s->isr & R_ISR_TXE_MASK) && (s->cr1 & R_CR1_TXEIE_MASK))        ||
        ((s->isr & R_ISR_TC_MASK) && (s->cr1 & R_CR1_TCIE_MASK))          ||
        ((s->isr & R_ISR_RXNE_MASK) && (s->cr1 & R_CR1_RXNEIE_MASK))      ||
        ((s->isr & R_ISR_IDLE_MASK) && (s->cr1 & R_CR1_IDLEIE_MASK))      ||
        ((s->isr & R_ISR_ORE_MASK) &&
            ((s->cr1 & R_CR1_RXNEIE_MASK) || (s->cr3 & R_CR3_EIE_MASK)))  ||
        ((s->isr & R_ISR_FE_MASK) && (s->cr3 & R_CR3_EIE_MASK))           ||
        ((s->isr & R_ISR_PE_MASK) && (s->cr1 & R_CR1_PEIE_MASK))) {
        qemu_irq_raise(s->irq);
        trace_stm32l4x5_usart_irq_raised(s->isr);
    } else {
        qemu_irq_lower(s->irq);
        trace_stm32l4x5_usart_irq_lowered();
    }
}

static int stm32l4x5_usart_base_can_receive(void *opaque)
{
    Stm32l4x5UsartBaseState *s = opaque;

    if (!(s->isr & R_ISR_RXNE_MASK)) {
        return 1;
    }

    return 0;
}

static void stm32l4x5_usart_base_receive(void *opaque, const uint8_t *buf,
                                         int size)
{
    Stm32l4x5UsartBaseState *s = opaque;

    if (!((s->cr1 & R_CR1_UE_MASK) && (s->cr1 & R_CR1_RE_MASK))) {
        trace_stm32l4x5_usart_receiver_not_enabled(
            FIELD_EX32(s->cr1, CR1, UE), FIELD_EX32(s->cr1, CR1, RE));
        return;
    }

    if (!(s->cr3 & R_CR3_OVRDIS_MASK) && (s->isr & R_ISR_RXNE_MASK)) {
        s->isr |= R_ISR_ORE_MASK;
        trace_stm32l4x5_usart_overrun_detected(s->rdr, *buf);
    } else {
        s->rdr = *buf;
        s->isr |= R_ISR_RXNE_MASK;
        trace_stm32l4x5_usart_rx(s->rdr);
    }

    stm32l4x5_update_irq(s);
}

static gboolean usart_transmit(void *do_not_use, GIOCondition cond,
                               void *opaque)
{
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(opaque);
    int ret;
    uint8_t ch = s->tdr;

    s->watch_tag = 0;

    if (!(s->cr1 & R_CR1_TE_MASK) || (s->isr & R_ISR_TXE_MASK)) {
        return G_SOURCE_REMOVE;
    }

    ret = qemu_chr_fe_write(&s->chr, &ch, 1);
    if (ret <= 0) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             usart_transmit, s);
        if (!s->watch_tag) {
            goto buffer_drained;
        }
        trace_stm32l4x5_usart_tx_pending();
        return G_SOURCE_REMOVE;
    }

buffer_drained:
    trace_stm32l4x5_usart_tx(ch);
    s->isr |= R_ISR_TC_MASK | R_ISR_TXE_MASK;
    stm32l4x5_update_irq(s);
    return G_SOURCE_REMOVE;
}

static void usart_cancel_transmit(Stm32l4x5UsartBaseState *s)
{
    if (s->watch_tag) {
        g_source_remove(s->watch_tag);
        s->watch_tag = 0;
    }
}

static void stm32l4x5_update_params(Stm32l4x5UsartBaseState *s)
{
    int speed, parity, data_bits, stop_bits;
    uint32_t value, usart_div;
    QEMUSerialSetParams ssp;

    if (s->cr1 & R_CR1_PCE_MASK) {
        if (s->cr1 & R_CR1_PS_MASK) {
            parity = 'O';
        } else {
            parity = 'E';
        }
    } else {
        parity = 'N';
    }

    switch (FIELD_EX32(s->cr2, CR2, STOP)) {
    case 0:
        stop_bits = 1;
        break;
    case 2:
        stop_bits = 2;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
            "UNIMPLEMENTED: fractionnal stop bits; CR2[13:12] = %u",
            FIELD_EX32(s->cr2, CR2, STOP));
        return;
    }

    switch ((FIELD_EX32(s->cr1, CR1, M1) << 1) | FIELD_EX32(s->cr1, CR1, M0)) {
    case 0:
        data_bits = 8;
        break;
    case 1:
        data_bits = 9;
        break;
    case 2:
        data_bits = 7;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "UNDEFINED: invalid word length, CR1.M = 0b11");
        return;
    }

    value = FIELD_EX32(s->brr, BRR, BRR);
    if (value < 16) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "UNDEFINED: BRR less than 16: %u", value);
        return;
    }

    if (FIELD_EX32(s->cr1, CR1, OVER8) == 0) {
        usart_div = value;
    } else {
        usart_div = ((value & 0xFFF0) | ((value & 0x0007) << 1)) / 2;
    }

    speed = clock_get_hz(s->clk) / usart_div;

    ssp.speed     = speed;
    ssp.parity    = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;

    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);

    trace_stm32l4x5_usart_update_params(speed, parity, data_bits, stop_bits);
}

static void stm32l4x5_usart_base_reset_hold(Object *obj, ResetType type)
{
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(obj);

    s->cr1 = 0x00000000;
    s->cr2 = 0x00000000;
    s->cr3 = 0x00000000;
    s->brr = 0x00000000;
    s->gtpr = 0x00000000;
    s->rtor = 0x00000000;
    s->isr = 0x020000C0;
    s->rdr = 0x00000000;
    s->tdr = 0x00000000;

    usart_cancel_transmit(s);
    stm32l4x5_update_irq(s);
}

static void usart_update_rqr(Stm32l4x5UsartBaseState *s, uint32_t value)
{
    if (value & R_RQR_RXFRQ_MASK) {
        s->isr &= ~R_ISR_RXNE_MASK;
    }
    stm32l4x5_update_irq(s);
}

static uint64_t stm32l4x5_usart_base_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32l4x5UsartBaseState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case A_CR1:
        retvalue = s->cr1;
        break;
    case A_CR2:
        retvalue = s->cr2;
        break;
    case A_CR3:
        retvalue = s->cr3;
        break;
    case A_BRR:
        retvalue = FIELD_EX32(s->brr, BRR, BRR);
        break;
    case A_GTPR:
        retvalue = s->gtpr;
        break;
    case A_RTOR:
        retvalue = s->rtor;
        break;
    case A_RQR:
        retvalue = 0x00000000;
        break;
    case A_ISR:
        retvalue = s->isr;
        break;
    case A_ICR:
        retvalue = 0x00000000;
        break;
    case A_RDR:
        retvalue = FIELD_EX32(s->rdr, RDR, RDR);
        s->isr &= ~R_ISR_RXNE_MASK;
        stm32l4x5_update_irq(s);
        break;
    case A_TDR:
        retvalue = FIELD_EX32(s->tdr, TDR, TDR);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    trace_stm32l4x5_usart_read(addr, retvalue);

    return retvalue;
}

static void stm32l4x5_usart_base_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32l4x5UsartBaseState *s = opaque;
    const uint32_t value = val64;

    trace_stm32l4x5_usart_write(addr, value);

    switch (addr) {
    case A_CR1:
        s->cr1 = value;
        stm32l4x5_update_params(s);
        stm32l4x5_update_isr(s);
        stm32l4x5_update_irq(s);
        return;
    case A_CR2:
        s->cr2 = value;
        stm32l4x5_update_params(s);
        return;
    case A_CR3:
        s->cr3 = value;
        return;
    case A_BRR:
        s->brr = value;
        stm32l4x5_update_params(s);
        return;
    case A_GTPR:
        s->gtpr = value;
        return;
    case A_RTOR:
        s->rtor = value;
        return;
    case A_RQR:
        usart_update_rqr(s, value);
        return;
    case A_ISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: ISR is read only !\n", __func__);
        return;
    case A_ICR:
        s->isr &= ~value;
        stm32l4x5_update_irq(s);
        return;
    case A_RDR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: RDR is read only !\n", __func__);
        return;
    case A_TDR:
        s->tdr = value;
        s->isr &= ~R_ISR_TXE_MASK;
        usart_transmit(NULL, G_IO_OUT, s);
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32l4x5_usart_base_ops = {
    .read = stm32l4x5_usart_base_read,
    .write = stm32l4x5_usart_base_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
};

static const Property stm32l4x5_usart_base_properties[] = {
    DEFINE_PROP_CHR("chardev", Stm32l4x5UsartBaseState, chr),
};

static void stm32l4x5_usart_base_init(Object *obj)
{
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_usart_base_ops, s,
                          TYPE_STM32L4X5_USART_BASE, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->clk = qdev_init_clock_in(DEVICE(s), "clk", NULL, s, 0);
}

static int stm32l4x5_usart_base_post_load(void *opaque, int version_id)
{
    Stm32l4x5UsartBaseState *s = (Stm32l4x5UsartBaseState *)opaque;

    stm32l4x5_update_params(s);
    return 0;
}

static const VMStateDescription vmstate_stm32l4x5_usart_base = {
    .name = TYPE_STM32L4X5_USART_BASE,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = stm32l4x5_usart_base_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr1, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(cr2, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(cr3, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(brr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(gtpr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(rtor, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(isr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(rdr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(tdr, Stm32l4x5UsartBaseState),
        VMSTATE_CLOCK(clk, Stm32l4x5UsartBaseState),
        VMSTATE_END_OF_LIST()
    }
};


static void stm32l4x5_usart_base_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(dev);
    if (!clock_has_source(s->clk)) {
        error_setg(errp, "USART clock must be wired up by SoC code");
        return;
    }

    qemu_chr_fe_set_handlers(&s->chr, stm32l4x5_usart_base_can_receive,
                             stm32l4x5_usart_base_receive, NULL, NULL,
                             s, NULL, true);
}

static void stm32l4x5_usart_base_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = stm32l4x5_usart_base_reset_hold;
    device_class_set_props(dc, stm32l4x5_usart_base_properties);
    dc->realize = stm32l4x5_usart_base_realize;
    dc->vmsd = &vmstate_stm32l4x5_usart_base;
}

static void stm32l4x5_usart_class_init(ObjectClass *oc, const void *data)
{
    Stm32l4x5UsartBaseClass *subc = STM32L4X5_USART_BASE_CLASS(oc);

    subc->type = STM32L4x5_USART;
}

static void stm32l4x5_uart_class_init(ObjectClass *oc, const void *data)
{
    Stm32l4x5UsartBaseClass *subc = STM32L4X5_USART_BASE_CLASS(oc);

    subc->type = STM32L4x5_UART;
}

static void stm32l4x5_lpuart_class_init(ObjectClass *oc, const void *data)
{
    Stm32l4x5UsartBaseClass *subc = STM32L4X5_USART_BASE_CLASS(oc);

    subc->type = STM32L4x5_LPUART;
}

static const TypeInfo stm32l4x5_usart_types[] = {
    {
        .name           = TYPE_STM32L4X5_USART_BASE,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32l4x5UsartBaseState),
        .instance_init  = stm32l4x5_usart_base_init,
        .class_size     = sizeof(Stm32l4x5UsartBaseClass),
        .class_init     = stm32l4x5_usart_base_class_init,
        .abstract       = true,
    }, {
        .name           = TYPE_STM32L4X5_USART,
        .parent         = TYPE_STM32L4X5_USART_BASE,
        .class_init     = stm32l4x5_usart_class_init,
    }, {
        .name           = TYPE_STM32L4X5_UART,
        .parent         = TYPE_STM32L4X5_USART_BASE,
        .class_init     = stm32l4x5_uart_class_init,
    }, {
        .name           = TYPE_STM32L4X5_LPUART,
        .parent         = TYPE_STM32L4X5_USART_BASE,
        .class_init     = stm32l4x5_lpuart_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_usart_types)
