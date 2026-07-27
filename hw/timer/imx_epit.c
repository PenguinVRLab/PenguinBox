/*
 * IMX EPIT Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 * Updated by Axel Heider
 *
 * This code is licensed under GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/timer/imx_epit.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/imx_ccm.h"
#include "qemu/module.h"
#include "qemu/log.h"

#ifndef DEBUG_IMX_EPIT
#define DEBUG_IMX_EPIT 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_EPIT) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_EPIT, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx_epit_reg_name(uint32_t reg)
{
    switch (reg) {
    case 0:
        return "CR";
    case 1:
        return "SR";
    case 2:
        return "LR";
    case 3:
        return "CMP";
    case 4:
        return "CNT";
    default:
        return "[?]";
    }
}

static const IMXClk imx_epit_clocks[] =  {
    CLK_NONE,
    CLK_IPG,
    CLK_IPG_HIGH,
    CLK_32k,
};

static void imx_epit_update_int(IMXEPITState *s)
{
    if ((s->sr & SR_OCIF) && (s->cr & CR_OCIEN) && (s->cr & CR_EN)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t imx_epit_get_freq(IMXEPITState *s)
{
    uint32_t clksrc = extract32(s->cr, CR_CLKSRC_SHIFT, CR_CLKSRC_BITS);
    uint32_t prescaler = 1 + extract32(s->cr, CR_PRESCALE_SHIFT, CR_PRESCALE_BITS);
    uint32_t f_in = imx_ccm_get_clock_frequency(s->ccm, imx_epit_clocks[clksrc]);
    uint32_t freq = f_in / prescaler;
    DPRINTF("ptimer frequency is %u\n", freq);
    return freq;
}

static void imx_epit_reset(IMXEPITState *s, bool is_hard_reset)
{
    if (is_hard_reset) {
        s->cr = 0;
    } else {
        s->cr &= (CR_EN|CR_ENMOD|CR_STOPEN|CR_DOZEN|CR_WAITEN|CR_DBGEN);
    }
    s->sr = 0;
    s->lr = EPIT_TIMER_MAX;
    s->cmp = 0;
    ptimer_transaction_begin(s->timer_cmp);
    ptimer_transaction_begin(s->timer_reload);

    assert(imx_epit_get_freq(s) == 0);
    ptimer_stop(s->timer_cmp);
    ptimer_stop(s->timer_reload);
    ptimer_set_limit(s->timer_cmp, EPIT_TIMER_MAX, 1);
    ptimer_set_limit(s->timer_reload, EPIT_TIMER_MAX, 1);
    ptimer_transaction_commit(s->timer_cmp);
    ptimer_transaction_commit(s->timer_reload);
}

static uint64_t imx_epit_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXEPITState *s = IMX_EPIT(opaque);
    uint32_t reg_value = 0;

    switch (offset >> 2) {
    case 0:
        reg_value = s->cr;
        break;

    case 1:
        reg_value = s->sr;
        break;

    case 2:
        reg_value = s->lr;
        break;

    case 3:
        reg_value = s->cmp;
        break;

    case 4:
        reg_value = ptimer_get_count(s->timer_reload);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_EPIT, __func__, offset);
        break;
    }

    DPRINTF("(%s) = 0x%08x\n", imx_epit_reg_name(offset >> 2), reg_value);

    return reg_value;
}

static void imx_epit_update_compare_timer(IMXEPITState *s)
{
    uint64_t counter = 0;
    bool is_oneshot = false;
    bool is_active = (s->cr & CR_EN) && imx_epit_get_freq(s);
    if (is_active) {
        counter = ptimer_get_count(s->timer_reload);
        uint64_t limit = ptimer_get_limit(s->timer_cmp);
        is_oneshot = (limit < s->cmp);
        if (counter >= s->cmp) {
            counter -= s->cmp;
        } else if (!is_oneshot) {
            counter += limit - s->cmp;
        } else {
            is_active = false;
        }
    }

    if (is_active) {
        ptimer_set_count(s->timer_cmp, counter);
        ptimer_run(s->timer_cmp, is_oneshot ? 1 : 0);
    } else {
        ptimer_stop(s->timer_cmp);
    }

}

static void imx_epit_write_cr(IMXEPITState *s, uint32_t value)
{
    uint32_t oldcr = s->cr;

    s->cr = value & 0x03ffffff;

    if (s->cr & CR_SWR) {
        imx_epit_reset(s, false);
    } else {
        uint32_t freq;
        uint32_t toggled_cr_bits = oldcr ^ s->cr;
        bool set_limit = toggled_cr_bits & CR_RLD;
        bool is_switched_on = (toggled_cr_bits & s->cr) & CR_EN;
        bool set_counter = is_switched_on && (s->cr & CR_ENMOD);

        ptimer_transaction_begin(s->timer_cmp);
        ptimer_transaction_begin(s->timer_reload);
        freq = imx_epit_get_freq(s);
        if (freq) {
            ptimer_set_freq(s->timer_reload, freq);
            ptimer_set_freq(s->timer_cmp, freq);
        }

        if (set_limit || set_counter) {
            uint64_t limit = (s->cr & CR_RLD) ? s->lr : EPIT_TIMER_MAX;
            ptimer_set_limit(s->timer_reload, limit, set_counter ? 1 : 0);
            if (set_limit) {
                ptimer_set_limit(s->timer_cmp, limit, 0);
            }
        }
        if (freq && (s->cr & CR_EN)) {
            ptimer_run(s->timer_reload, 0);
        } else {
            ptimer_stop(s->timer_reload);
        }
        ptimer_transaction_commit(s->timer_reload);
        imx_epit_update_compare_timer(s);
        ptimer_transaction_commit(s->timer_cmp);
    }

    imx_epit_update_int(s);
}

static void imx_epit_write_sr(IMXEPITState *s, uint32_t value)
{
    if (value & SR_OCIF) {
        s->sr = 0;
        imx_epit_update_int(s);
    }
}

static void imx_epit_write_lr(IMXEPITState *s, uint32_t value)
{
    s->lr = value;

    ptimer_transaction_begin(s->timer_cmp);
    ptimer_transaction_begin(s->timer_reload);
    if (s->cr & CR_RLD) {
        ptimer_set_limit(s->timer_reload, s->lr, s->cr & CR_IOVW);
        ptimer_set_limit(s->timer_cmp, s->lr, 0);
    } else if (s->cr & CR_IOVW) {
        ptimer_set_count(s->timer_reload, s->lr);
    }
    ptimer_transaction_commit(s->timer_reload);
    imx_epit_update_compare_timer(s);
    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write_cmp(IMXEPITState *s, uint32_t value)
{
    s->cmp = value;

    ptimer_transaction_begin(s->timer_cmp);
    imx_epit_update_compare_timer(s);
    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXEPITState *s = IMX_EPIT(opaque);

    DPRINTF("(%s, value = 0x%08x)\n", imx_epit_reg_name(offset >> 2),
            (uint32_t)value);

    switch (offset >> 2) {
    case 0:
        imx_epit_write_cr(s, (uint32_t)value);
        break;

    case 1:
        imx_epit_write_sr(s, (uint32_t)value);
        break;

    case 2:
        imx_epit_write_lr(s, (uint32_t)value);
        break;

    case 3:
        imx_epit_write_cmp(s, (uint32_t)value);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_EPIT, __func__, offset);
        break;
    }
}

static void imx_epit_cmp(void *opaque)
{
    IMXEPITState *s = IMX_EPIT(opaque);

    assert(s->cr & CR_EN);

    DPRINTF("sr was %d\n", s->sr);
    s->sr |= SR_OCIF;
    imx_epit_update_int(s);
}

static void imx_epit_reload(void *opaque)
{
}

static const MemoryRegionOps imx_epit_ops = {
    .read = imx_epit_read,
    .write = imx_epit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_imx_timer_epit = {
    .name = TYPE_IMX_EPIT,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr, IMXEPITState),
        VMSTATE_UINT32(sr, IMXEPITState),
        VMSTATE_UINT32(lr, IMXEPITState),
        VMSTATE_UINT32(cmp, IMXEPITState),
        VMSTATE_PTIMER(timer_reload, IMXEPITState),
        VMSTATE_PTIMER(timer_cmp, IMXEPITState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx_epit_realize(DeviceState *dev, Error **errp)
{
    IMXEPITState *s = IMX_EPIT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    DPRINTF("\n");

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &imx_epit_ops, s, TYPE_IMX_EPIT,
                          0x00001000);
    sysbus_init_mmio(sbd, &s->iomem);

    s->timer_reload = ptimer_init(imx_epit_reload, s, PTIMER_POLICY_LEGACY);

    s->timer_cmp = ptimer_init(imx_epit_cmp, s, PTIMER_POLICY_LEGACY);
}

static void imx_epit_dev_reset(DeviceState *dev)
{
    IMXEPITState *s = IMX_EPIT(dev);
    imx_epit_reset(s, true);
}

static void imx_epit_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc  = DEVICE_CLASS(klass);

    dc->realize = imx_epit_realize;
    device_class_set_legacy_reset(dc, imx_epit_dev_reset);
    dc->vmsd = &vmstate_imx_timer_epit;
    dc->desc = "i.MX periodic timer";
}

static const TypeInfo imx_epit_info = {
    .name = TYPE_IMX_EPIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXEPITState),
    .class_init = imx_epit_class_init,
};

static void imx_epit_register_types(void)
{
    type_register_static(&imx_epit_info);
}

type_init(imx_epit_register_types)
