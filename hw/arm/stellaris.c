/*
 * Luminary Micro Stellaris peripherals
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "hw/core/split-irq.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "hw/ssi/ssi.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "qemu/timer.h"
#include "hw/i2c/i2c.h"
#include "net/net.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/arm/armv7m.h"
#include "hw/char/pl011.h"
#include "hw/input/stellaris_gamepad.h"
#include "hw/irq.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"
#include "migration/vmstate.h"
#include "hw/misc/unimp.h"
#include "hw/timer/stellaris-gptm.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"
#include "qobject/qlist.h"
#include "ui/input.h"

#define GPIO_A 0
#define GPIO_B 1
#define GPIO_C 2
#define GPIO_D 3
#define GPIO_E 4
#define GPIO_F 5
#define GPIO_G 6

#define BP_OLED_I2C  0x01
#define BP_OLED_SSI  0x02
#define BP_GAMEPAD   0x04

#define NUM_IRQ_LINES 64
#define NUM_PRIO_BITS 3

#define NUM_GPIO    7
#define NUM_UART    4
#define NUM_GPTM    4
#define NUM_I2C     2

#define DC1_WDT          3
#define DC1_HIB          6
#define DC1_MPU          7
#define DC1_ADC          16
#define DC1_PWM          20
#define DC2_UART(n)     (n)
#define DC2_SSI          4
#define DC2_QEI(n)      (8 + n)
#define DC2_I2C(n)      (12 + 2 * n)
#define DC2_GPTM(n)     (16 + n)
#define DC2_COMP(n)     (24 + n)
#define DC4_GPIO(n)     (n)
#define DC4_EMAC         28

#define DEV_CAP(_dc, _cap) extract32(board->dc##_dc, DC##_dc##_##_cap, 1)

typedef const struct {
    const char *name;
    uint32_t did0;
    uint32_t did1;
    uint32_t dc0;
    uint32_t dc1;
    uint32_t dc2;
    uint32_t dc3;
    uint32_t dc4;
    uint32_t peripherals;
} stellaris_board_info;

#define TYPE_STELLARIS_SYS "stellaris-sys"
OBJECT_DECLARE_SIMPLE_TYPE(ssys_state, STELLARIS_SYS)

struct ssys_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t pborctl;
    uint32_t ldopctl;
    uint32_t int_status;
    uint32_t int_mask;
    uint32_t resc;
    uint32_t rcc;
    uint32_t rcc2;
    uint32_t rcgc[3];
    uint32_t scgc[3];
    uint32_t dcgc[3];
    uint32_t clkvclr;
    uint32_t ldoarst;
    qemu_irq irq;
    Clock *sysclk;
    uint32_t user0;
    uint32_t user1;
    uint32_t did0;
    uint32_t did1;
    uint32_t dc0;
    uint32_t dc1;
    uint32_t dc2;
    uint32_t dc3;
    uint32_t dc4;
};

static void ssys_update(ssys_state *s)
{
  qemu_set_irq(s->irq, (s->int_status & s->int_mask) != 0);
}

static const uint32_t pllcfg_sandstorm[16] = {
    0x31c0,
    0x1ae0,
    0x18c0,
    0xd573,
    0x37a6,
    0x1ae2,
    0x0c40,
    0x98bc,
    0x935b,
    0x09c0,
    0x4dee,
    0x0c41,
    0x75db,
    0x1ae6,
    0x0600,
    0x585b
};

static const uint32_t pllcfg_fury[16] = {
    0x3200,
    0x1b20,
    0x1900,
    0xf42b,
    0x37e3,
    0x1b21,
    0x0c80,
    0x98ee,
    0xd5b4,
    0x0a00,
    0x4e27,
    0x1902,
    0xec1c,
    0x1b23,
    0x0640,
    0xb11c
};

#define DID0_VER_MASK        0x70000000
#define DID0_VER_0           0x00000000
#define DID0_VER_1           0x10000000

#define DID0_CLASS_MASK      0x00FF0000
#define DID0_CLASS_SANDSTORM 0x00000000
#define DID0_CLASS_FURY      0x00010000

static int ssys_board_class(const ssys_state *s)
{
    uint32_t did0 = s->did0;
    switch (did0 & DID0_VER_MASK) {
    case DID0_VER_0:
        return DID0_CLASS_SANDSTORM;
    case DID0_VER_1:
        switch (did0 & DID0_CLASS_MASK) {
        case DID0_CLASS_SANDSTORM:
        case DID0_CLASS_FURY:
            return did0 & DID0_CLASS_MASK;
        }
        /* for unknown classes, fall through */
    default:
        g_assert_not_reached();
    }
}

static uint64_t ssys_read(void *opaque, hwaddr offset,
                          unsigned size)
{
    ssys_state *s = (ssys_state *)opaque;

    switch (offset) {
    case 0x000:
        return s->did0;
    case 0x004:
        return s->did1;
    case 0x008:
        return s->dc0;
    case 0x010:
        return s->dc1;
    case 0x014:
        return s->dc2;
    case 0x018:
        return s->dc3;
    case 0x01c:
        return s->dc4;
    case 0x030:
        return s->pborctl;
    case 0x034:
        return s->ldopctl;
    case 0x040:
        return 0;
    case 0x044:
        return 0;
    case 0x048:
        return 0;
    case 0x050:
        return s->int_status;
    case 0x054:
        return s->int_mask;
    case 0x058:
        return s->int_status & s->int_mask;
    case 0x05c:
        return s->resc;
    case 0x060:
        return s->rcc;
    case 0x064:
        {
            int xtal;
            xtal = (s->rcc >> 6) & 0xf;
            switch (ssys_board_class(s)) {
            case DID0_CLASS_FURY:
                return pllcfg_fury[xtal];
            case DID0_CLASS_SANDSTORM:
                return pllcfg_sandstorm[xtal];
            default:
                g_assert_not_reached();
            }
        }
    case 0x070:
        return s->rcc2;
    case 0x100:
        return s->rcgc[0];
    case 0x104:
        return s->rcgc[1];
    case 0x108:
        return s->rcgc[2];
    case 0x110:
        return s->scgc[0];
    case 0x114:
        return s->scgc[1];
    case 0x118:
        return s->scgc[2];
    case 0x120:
        return s->dcgc[0];
    case 0x124:
        return s->dcgc[1];
    case 0x128:
        return s->dcgc[2];
    case 0x150:
        return s->clkvclr;
    case 0x160:
        return s->ldoarst;
    case 0x1e0:
        return s->user0;
    case 0x1e4:
        return s->user1;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSYS: read at bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static bool ssys_use_rcc2(ssys_state *s)
{
    return (s->rcc2 >> 31) & 0x1;
}

static void ssys_calculate_system_clock(ssys_state *s, bool propagate_clock)
{
    int period_ns;
    if (ssys_use_rcc2(s)) {
        period_ns = 5 * (((s->rcc2 >> 23) & 0x3f) + 1);
    } else {
        period_ns = 5 * (((s->rcc >> 23) & 0xf) + 1);
    }
    clock_set_ns(s->sysclk, period_ns);
    if (propagate_clock) {
        clock_propagate(s->sysclk);
    }
}

static void ssys_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{
    ssys_state *s = (ssys_state *)opaque;

    switch (offset) {
    case 0x030:
        s->pborctl = value & 0xffff;
        break;
    case 0x034:
        s->ldopctl = value & 0x1f;
        break;
    case 0x040:
    case 0x044:
    case 0x048:
        qemu_log_mask(LOG_UNIMP, "Peripheral reset not implemented\n");
        break;
    case 0x054:
        s->int_mask = value & 0x7f;
        break;
    case 0x058:
        s->int_status &= ~value;
        break;
    case 0x05c:
        s->resc = value & 0x3f;
        break;
    case 0x060:
        if ((s->rcc & (1 << 13)) != 0 && (value & (1 << 13)) == 0) {
            s->int_status |= (1 << 6);
        }
        s->rcc = value;
        ssys_calculate_system_clock(s, true);
        break;
    case 0x070:
        if (ssys_board_class(s) == DID0_CLASS_SANDSTORM) {
            break;
        }

        if ((s->rcc2 & (1 << 13)) != 0 && (value & (1 << 13)) == 0) {
            s->int_status |= (1 << 6);
        }
        s->rcc2 = value;
        ssys_calculate_system_clock(s, true);
        break;
    case 0x100:
        s->rcgc[0] = value;
        break;
    case 0x104:
        s->rcgc[1] = value;
        break;
    case 0x108:
        s->rcgc[2] = value;
        break;
    case 0x110:
        s->scgc[0] = value;
        break;
    case 0x114:
        s->scgc[1] = value;
        break;
    case 0x118:
        s->scgc[2] = value;
        break;
    case 0x120:
        s->dcgc[0] = value;
        break;
    case 0x124:
        s->dcgc[1] = value;
        break;
    case 0x128:
        s->dcgc[2] = value;
        break;
    case 0x150:
        s->clkvclr = value;
        break;
    case 0x160:
        s->ldoarst = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSYS: write at bad offset 0x%x\n", (int)offset);
    }
    ssys_update(s);
}

static const MemoryRegionOps ssys_ops = {
    .read = ssys_read,
    .write = ssys_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stellaris_sys_reset_enter(Object *obj, ResetType type)
{
    ssys_state *s = STELLARIS_SYS(obj);

    s->pborctl = 0x7ffd;
    s->rcc = 0x078e3ac0;

    if (ssys_board_class(s) == DID0_CLASS_SANDSTORM) {
        s->rcc2 = 0;
    } else {
        s->rcc2 = 0x07802810;
    }
    s->rcgc[0] = 1;
    s->scgc[0] = 1;
    s->dcgc[0] = 1;
}

static void stellaris_sys_reset_hold(Object *obj, ResetType type)
{
    ssys_state *s = STELLARIS_SYS(obj);

    ssys_calculate_system_clock(s, true);
}

static void stellaris_sys_reset_exit(Object *obj, ResetType type)
{
}

static int stellaris_sys_post_load(void *opaque, int version_id)
{
    ssys_state *s = opaque;

    ssys_calculate_system_clock(s, false);

    return 0;
}

static const VMStateDescription vmstate_stellaris_sys = {
    .name = "stellaris_sys",
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = stellaris_sys_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(pborctl, ssys_state),
        VMSTATE_UINT32(ldopctl, ssys_state),
        VMSTATE_UINT32(int_mask, ssys_state),
        VMSTATE_UINT32(int_status, ssys_state),
        VMSTATE_UINT32(resc, ssys_state),
        VMSTATE_UINT32(rcc, ssys_state),
        VMSTATE_UINT32_V(rcc2, ssys_state, 2),
        VMSTATE_UINT32_ARRAY(rcgc, ssys_state, 3),
        VMSTATE_UINT32_ARRAY(scgc, ssys_state, 3),
        VMSTATE_UINT32_ARRAY(dcgc, ssys_state, 3),
        VMSTATE_UINT32(clkvclr, ssys_state),
        VMSTATE_UINT32(ldoarst, ssys_state),
        VMSTATE_END_OF_LIST()
    }
};

static const Property stellaris_sys_properties[] = {
    DEFINE_PROP_UINT32("user0", ssys_state, user0, 0),
    DEFINE_PROP_UINT32("user1", ssys_state, user1, 0),
    DEFINE_PROP_UINT32("did0", ssys_state, did0, 0),
    DEFINE_PROP_UINT32("did1", ssys_state, did1, 0),
    DEFINE_PROP_UINT32("dc0", ssys_state, dc0, 0),
    DEFINE_PROP_UINT32("dc1", ssys_state, dc1, 0),
    DEFINE_PROP_UINT32("dc2", ssys_state, dc2, 0),
    DEFINE_PROP_UINT32("dc3", ssys_state, dc3, 0),
    DEFINE_PROP_UINT32("dc4", ssys_state, dc4, 0),
};

static void stellaris_sys_instance_init(Object *obj)
{
    ssys_state *s = STELLARIS_SYS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);

    memory_region_init_io(&s->iomem, obj, &ssys_ops, s, "ssys", 0x00001000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->sysclk = qdev_init_clock_out(DEVICE(s), "SYSCLK");
}

#define TYPE_STELLARIS_I2C "stellaris-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(stellaris_i2c_state, STELLARIS_I2C)

struct stellaris_i2c_state {
    SysBusDevice parent_obj;

    I2CBus *bus;
    qemu_irq irq;
    MemoryRegion iomem;
    uint32_t msa;
    uint32_t mcs;
    uint32_t mdr;
    uint32_t mtpr;
    uint32_t mimr;
    uint32_t mris;
    uint32_t mcr;
};

#define STELLARIS_I2C_MCS_BUSY    0x01
#define STELLARIS_I2C_MCS_ERROR   0x02
#define STELLARIS_I2C_MCS_ADRACK  0x04
#define STELLARIS_I2C_MCS_DATACK  0x08
#define STELLARIS_I2C_MCS_ARBLST  0x10
#define STELLARIS_I2C_MCS_IDLE    0x20
#define STELLARIS_I2C_MCS_BUSBSY  0x40

static uint64_t stellaris_i2c_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    stellaris_i2c_state *s = (stellaris_i2c_state *)opaque;

    switch (offset) {
    case 0x00:
        return s->msa;
    case 0x04:
        return s->mcs | STELLARIS_I2C_MCS_IDLE;
    case 0x08:
        return s->mdr;
    case 0x0c:
        return s->mtpr;
    case 0x10:
        return s->mimr;
    case 0x14:
        return s->mris;
    case 0x18:
        return s->mris & s->mimr;
    case 0x20:
        return s->mcr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stellaris_i2c: read at bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void stellaris_i2c_update(stellaris_i2c_state *s)
{
    int level;

    level = (s->mris & s->mimr) != 0;
    qemu_set_irq(s->irq, level);
}

static void stellaris_i2c_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    stellaris_i2c_state *s = (stellaris_i2c_state *)opaque;

    switch (offset) {
    case 0x00:
        s->msa = value & 0xff;
        break;
    case 0x04:
        if ((s->mcr & 0x10) == 0) {
            break;
        }
        if ((value & 2) && (s->mcs & STELLARIS_I2C_MCS_BUSBSY) == 0) {
            if (i2c_start_transfer(s->bus, s->msa >> 1, s->msa & 1)) {
                s->mcs |= STELLARIS_I2C_MCS_ARBLST;
            } else {
                s->mcs &= ~STELLARIS_I2C_MCS_ARBLST;
                s->mcs |= STELLARIS_I2C_MCS_BUSBSY;
            }
        }
        if (!i2c_bus_busy(s->bus)
                || (s->mcs & STELLARIS_I2C_MCS_BUSBSY) == 0) {
            s->mcs |= STELLARIS_I2C_MCS_ERROR;
            break;
        }
        s->mcs &= ~STELLARIS_I2C_MCS_ERROR;
        if (value & 1) {
            if (s->msa & 1) {
                s->mdr = i2c_recv(s->bus);
            } else {
                i2c_send(s->bus, s->mdr);
            }
            s->mris |= 1;
        }
        if (value & 4) {
            i2c_end_transfer(s->bus);
            s->mcs &= ~STELLARIS_I2C_MCS_BUSBSY;
        }
        break;
    case 0x08:
        s->mdr = value & 0xff;
        break;
    case 0x0c:
        s->mtpr = value & 0xff;
        break;
    case 0x10:
        s->mimr = 1;
        break;
    case 0x1c:
        s->mris &= ~value;
        break;
    case 0x20:
        if (value & 1) {
            qemu_log_mask(LOG_UNIMP,
                          "stellaris_i2c: Loopback not implemented\n");
        }
        if (value & 0x20) {
            qemu_log_mask(LOG_UNIMP,
                          "stellaris_i2c: Slave mode not implemented\n");
        }
        s->mcr = value & 0x31;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stellaris_i2c: write at bad offset 0x%x\n", (int)offset);
    }
    stellaris_i2c_update(s);
}

static void stellaris_i2c_reset_enter(Object *obj, ResetType type)
{
    stellaris_i2c_state *s = STELLARIS_I2C(obj);

    if (s->mcs & STELLARIS_I2C_MCS_BUSBSY)
        i2c_end_transfer(s->bus);
}

static void stellaris_i2c_reset_hold(Object *obj, ResetType type)
{
    stellaris_i2c_state *s = STELLARIS_I2C(obj);

    s->msa = 0;
    s->mcs = 0;
    s->mdr = 0;
    s->mtpr = 1;
    s->mimr = 0;
    s->mris = 0;
    s->mcr = 0;
}

static void stellaris_i2c_reset_exit(Object *obj, ResetType type)
{
    stellaris_i2c_state *s = STELLARIS_I2C(obj);

    stellaris_i2c_update(s);
}

static const MemoryRegionOps stellaris_i2c_ops = {
    .read = stellaris_i2c_read,
    .write = stellaris_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stellaris_i2c = {
    .name = "stellaris_i2c",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(msa, stellaris_i2c_state),
        VMSTATE_UINT32(mcs, stellaris_i2c_state),
        VMSTATE_UINT32(mdr, stellaris_i2c_state),
        VMSTATE_UINT32(mtpr, stellaris_i2c_state),
        VMSTATE_UINT32(mimr, stellaris_i2c_state),
        VMSTATE_UINT32(mris, stellaris_i2c_state),
        VMSTATE_UINT32(mcr, stellaris_i2c_state),
        VMSTATE_END_OF_LIST()
    }
};

static void stellaris_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    stellaris_i2c_state *s = STELLARIS_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    I2CBus *bus;

    sysbus_init_irq(sbd, &s->irq);
    bus = i2c_init_bus(dev, "i2c");
    s->bus = bus;

    memory_region_init_io(&s->iomem, obj, &stellaris_i2c_ops, s,
                          "i2c", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

#define STELLARIS_ADC_EM_CONTROLLER 0
#define STELLARIS_ADC_EM_COMP       1
#define STELLARIS_ADC_EM_EXTERNAL   4
#define STELLARIS_ADC_EM_TIMER      5
#define STELLARIS_ADC_EM_PWM0       6
#define STELLARIS_ADC_EM_PWM1       7
#define STELLARIS_ADC_EM_PWM2       8

#define STELLARIS_ADC_FIFO_EMPTY    0x0100
#define STELLARIS_ADC_FIFO_FULL     0x1000

#define TYPE_STELLARIS_ADC "stellaris-adc"
typedef struct StellarisADCState StellarisADCState;
DECLARE_INSTANCE_CHECKER(StellarisADCState, STELLARIS_ADC, TYPE_STELLARIS_ADC)

struct StellarisADCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t actss;
    uint32_t ris;
    uint32_t im;
    uint32_t emux;
    uint32_t ostat;
    uint32_t ustat;
    uint32_t sspri;
    uint32_t sac;
    struct {
        uint32_t state;
        uint32_t data[16];
    } fifo[4];
    uint32_t ssmux[4];
    uint32_t ssctl[4];
    uint32_t noise;
    qemu_irq irq[4];
};

static uint32_t stellaris_adc_fifo_read(StellarisADCState *s, int n)
{
    int tail;

    tail = s->fifo[n].state & 0xf;
    if (s->fifo[n].state & STELLARIS_ADC_FIFO_EMPTY) {
        s->ustat |= 1 << n;
    } else {
        s->fifo[n].state = (s->fifo[n].state & ~0xf) | ((tail + 1) & 0xf);
        s->fifo[n].state &= ~STELLARIS_ADC_FIFO_FULL;
        if (tail + 1 == ((s->fifo[n].state >> 4) & 0xf))
            s->fifo[n].state |= STELLARIS_ADC_FIFO_EMPTY;
    }
    return s->fifo[n].data[tail];
}

static void stellaris_adc_fifo_write(StellarisADCState *s, int n,
                                     uint32_t value)
{
    int head;

    head = (s->fifo[n].state >> 4) & 0xf;
    if (s->fifo[n].state & STELLARIS_ADC_FIFO_FULL) {
        s->ostat |= 1 << n;
        return;
    }
    s->fifo[n].data[head] = value;
    head = (head + 1) & 0xf;
    s->fifo[n].state &= ~STELLARIS_ADC_FIFO_EMPTY;
    s->fifo[n].state = (s->fifo[n].state & ~0xf0) | (head << 4);
    if ((s->fifo[n].state & 0xf) == head)
        s->fifo[n].state |= STELLARIS_ADC_FIFO_FULL;
}

static void stellaris_adc_update(StellarisADCState *s)
{
    int level;
    int n;

    for (n = 0; n < 4; n++) {
        level = (s->ris & s->im & (1 << n)) != 0;
        qemu_set_irq(s->irq[n], level);
    }
}

static void stellaris_adc_trigger(void *opaque, int irq, int level)
{
    StellarisADCState *s = opaque;
    int n;

    for (n = 0; n < 4; n++) {
        if ((s->actss & (1 << n)) == 0) {
            continue;
        }

        if (((s->emux >> (n * 4)) & 0xff) != 5) {
            continue;
        }

        s->noise = s->noise * 314159 + 1;
        stellaris_adc_fifo_write(s, n, 0x200 + ((s->noise >> 16) & 7));
        s->ris |= (1 << n);
        stellaris_adc_update(s);
    }
}

static void stellaris_adc_reset_hold(Object *obj, ResetType type)
{
    StellarisADCState *s = STELLARIS_ADC(obj);
    int n;

    for (n = 0; n < 4; n++) {
        s->ssmux[n] = 0;
        s->ssctl[n] = 0;
        s->fifo[n].state = STELLARIS_ADC_FIFO_EMPTY;
    }
}

static uint64_t stellaris_adc_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    StellarisADCState *s = opaque;

    if (offset >= 0x40 && offset < 0xc0) {
        int n;
        n = (offset - 0x40) >> 5;
        switch (offset & 0x1f) {
        case 0x00:
            return s->ssmux[n];
        case 0x04:
            return s->ssctl[n];
        case 0x08:
            return stellaris_adc_fifo_read(s, n);
        case 0x0c:
            return s->fifo[n].state;
        default:
            break;
        }
    }
    switch (offset) {
    case 0x00:
        return s->actss;
    case 0x04:
        return s->ris;
    case 0x08:
        return s->im;
    case 0x0c:
        return s->ris & s->im;
    case 0x10:
        return s->ostat;
    case 0x14:
        return s->emux;
    case 0x18:
        return s->ustat;
    case 0x20:
        return s->sspri;
    case 0x30:
        return s->sac;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stellaris_adc: read at bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void stellaris_adc_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    StellarisADCState *s = opaque;

    if (offset >= 0x40 && offset < 0xc0) {
        int n;
        n = (offset - 0x40) >> 5;
        switch (offset & 0x1f) {
        case 0x00:
            s->ssmux[n] = value & 0x33333333;
            return;
        case 0x04:
            if (value != 6) {
                qemu_log_mask(LOG_UNIMP,
                              "ADC: Unimplemented sequence %" PRIx64 "\n",
                              value);
            }
            s->ssctl[n] = value;
            return;
        default:
            break;
        }
    }
    switch (offset) {
    case 0x00:
        s->actss = value & 0xf;
        break;
    case 0x08:
        s->im = value;
        break;
    case 0x0c:
        s->ris &= ~value;
        break;
    case 0x10:
        s->ostat &= ~value;
        break;
    case 0x14:
        s->emux = value;
        break;
    case 0x18:
        s->ustat &= ~value;
        break;
    case 0x20:
        s->sspri = value;
        break;
    case 0x28:
        qemu_log_mask(LOG_UNIMP, "ADC: sample initiate unimplemented\n");
        break;
    case 0x30:
        s->sac = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "stellaris_adc: write at bad offset 0x%x\n", (int)offset);
    }
    stellaris_adc_update(s);
}

static const MemoryRegionOps stellaris_adc_ops = {
    .read = stellaris_adc_read,
    .write = stellaris_adc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stellaris_adc = {
    .name = "stellaris_adc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(actss, StellarisADCState),
        VMSTATE_UINT32(ris, StellarisADCState),
        VMSTATE_UINT32(im, StellarisADCState),
        VMSTATE_UINT32(emux, StellarisADCState),
        VMSTATE_UINT32(ostat, StellarisADCState),
        VMSTATE_UINT32(ustat, StellarisADCState),
        VMSTATE_UINT32(sspri, StellarisADCState),
        VMSTATE_UINT32(sac, StellarisADCState),
        VMSTATE_UINT32(fifo[0].state, StellarisADCState),
        VMSTATE_UINT32_ARRAY(fifo[0].data, StellarisADCState, 16),
        VMSTATE_UINT32(ssmux[0], StellarisADCState),
        VMSTATE_UINT32(ssctl[0], StellarisADCState),
        VMSTATE_UINT32(fifo[1].state, StellarisADCState),
        VMSTATE_UINT32_ARRAY(fifo[1].data, StellarisADCState, 16),
        VMSTATE_UINT32(ssmux[1], StellarisADCState),
        VMSTATE_UINT32(ssctl[1], StellarisADCState),
        VMSTATE_UINT32(fifo[2].state, StellarisADCState),
        VMSTATE_UINT32_ARRAY(fifo[2].data, StellarisADCState, 16),
        VMSTATE_UINT32(ssmux[2], StellarisADCState),
        VMSTATE_UINT32(ssctl[2], StellarisADCState),
        VMSTATE_UINT32(fifo[3].state, StellarisADCState),
        VMSTATE_UINT32_ARRAY(fifo[3].data, StellarisADCState, 16),
        VMSTATE_UINT32(ssmux[3], StellarisADCState),
        VMSTATE_UINT32(ssctl[3], StellarisADCState),
        VMSTATE_UINT32(noise, StellarisADCState),
        VMSTATE_END_OF_LIST()
    }
};

static void stellaris_adc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    StellarisADCState *s = STELLARIS_ADC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int n;

    for (n = 0; n < 4; n++) {
        sysbus_init_irq(sbd, &s->irq[n]);
    }

    memory_region_init_io(&s->iomem, obj, &stellaris_adc_ops, s,
                          "adc", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(dev, stellaris_adc_trigger, 1);
}

static const stellaris_board_info stellaris_boards[] = {
  { "LM3S811EVB",
    0,
    0x0032000e,
    0x001f001f,
    0x001132bf,
    0x01071013,
    0x3f0f01ff,
    0x0000001f,
    BP_OLED_I2C
  },
  { "LM3S6965EVB",
    0x10010002,
    0x1073402e,
    0x00ff007f,
    0x001133ff,
    0x030f5317,
    0x0f0f87ff,
    0x5000007f,
    BP_OLED_SSI | BP_GAMEPAD
  }
};

static void stellaris_init(MachineState *ms, stellaris_board_info *board)
{
    static const int uart_irq[NUM_UART] = {5, 6, 33, 34};
    static const int timer_irq[NUM_GPTM] = {19, 21, 23, 35};
    static const uint32_t gpio_addr[NUM_GPIO] =
      { 0x40004000, 0x40005000, 0x40006000, 0x40007000,
        0x40024000, 0x40025000, 0x40026000};
    static const int gpio_irq[NUM_GPIO] = {0, 1, 2, 3, 4, 30, 31};
    static const uint32_t i2c_addr[NUM_I2C] = {0x40020000, 0x40021000};
    static const int i2c_irq[NUM_I2C] = {8, 37};

    Object *soc_container;
    DeviceState *gpio_dev[NUM_GPIO], *armv7m, *nvic;
    qemu_irq gpio_in[NUM_GPIO][8];
    qemu_irq gpio_out[NUM_GPIO][8];
    qemu_irq adc;
    int sram_size;
    int flash_size;
    DeviceState *i2c_dev[NUM_I2C] = { };
    DeviceState *dev;
    DeviceState *ssys_dev;
    int i;
    int j;
    NICInfo *nd;
    MACAddr mac;

    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *system_memory = get_system_memory();

    flash_size = (((board->dc0 & 0xffff) + 1) << 1) * 1024;
    sram_size = ((board->dc0 >> 18) + 1) * 1024;

    soc_container = object_new(TYPE_CONTAINER);
    object_property_add_child(OBJECT(ms), "soc", soc_container);

    memory_region_init_rom(flash, NULL, "stellaris.flash", flash_size,
                           &error_fatal);
    memory_region_add_subregion(system_memory, 0, flash);

    memory_region_init_ram(sram, NULL, "stellaris.sram", sram_size,
                           &error_fatal);
    memory_region_add_subregion(system_memory, 0x20000000, sram);

    ssys_dev = qdev_new(TYPE_STELLARIS_SYS);
    object_property_add_child(soc_container, "sys", OBJECT(ssys_dev));

    nd = qemu_find_nic_info("stellaris_enet", true, "stellaris");
    if (nd) {
        memcpy(mac.a, nd->macaddr.a, sizeof(mac.a));
    } else {
        qemu_macaddr_default_if_unset(&mac);
    }

    qdev_prop_set_uint32(ssys_dev, "user0",
                         mac.a[0] | (mac.a[1] << 8) | (mac.a[2] << 16));
    qdev_prop_set_uint32(ssys_dev, "user1",
                         mac.a[3] | (mac.a[4] << 8) | (mac.a[5] << 16));
    qdev_prop_set_uint32(ssys_dev, "did0", board->did0);
    qdev_prop_set_uint32(ssys_dev, "did1", board->did1);
    qdev_prop_set_uint32(ssys_dev, "dc0", board->dc0);
    qdev_prop_set_uint32(ssys_dev, "dc1", board->dc1);
    qdev_prop_set_uint32(ssys_dev, "dc2", board->dc2);
    qdev_prop_set_uint32(ssys_dev, "dc3", board->dc3);
    qdev_prop_set_uint32(ssys_dev, "dc4", board->dc4);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ssys_dev), &error_fatal);

    armv7m = qdev_new(TYPE_ARMV7M);
    object_property_add_child(soc_container, "v7m", OBJECT(armv7m));
    qdev_prop_set_uint32(armv7m, "num-irq", NUM_IRQ_LINES);
    qdev_prop_set_uint8(armv7m, "num-prio-bits", NUM_PRIO_BITS);
    qdev_prop_set_string(armv7m, "cpu-type", ms->cpu_type);
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk",
                          qdev_get_clock_out(ssys_dev, "SYSCLK"));
    object_property_set_link(OBJECT(armv7m), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(armv7m), &error_fatal);
    nvic = armv7m;

    sysbus_mmio_map(SYS_BUS_DEVICE(ssys_dev), 0, 0x400fe000);
    sysbus_connect_irq(SYS_BUS_DEVICE(ssys_dev), 0, qdev_get_gpio_in(nvic, 28));

    if (DEV_CAP(1, ADC)) {
        dev = sysbus_create_varargs(TYPE_STELLARIS_ADC, 0x40038000,
                                    qdev_get_gpio_in(nvic, 14),
                                    qdev_get_gpio_in(nvic, 15),
                                    qdev_get_gpio_in(nvic, 16),
                                    qdev_get_gpio_in(nvic, 17),
                                    NULL);
        adc = qdev_get_gpio_in(dev, 0);
    } else {
        adc = NULL;
    }
    for (i = 0; i < NUM_GPTM; i++) {
        if (DEV_CAP(2, GPTM(i))) {
            SysBusDevice *sbd;

            dev = qdev_new(TYPE_STELLARIS_GPTM);
            sbd = SYS_BUS_DEVICE(dev);
            object_property_add_child(soc_container, "gptm[*]", OBJECT(dev));
            qdev_connect_clock_in(dev, "clk",
                                  qdev_get_clock_out(ssys_dev, "SYSCLK"));
            sysbus_realize_and_unref(sbd, &error_fatal);
            sysbus_mmio_map(sbd, 0, 0x40030000 + i * 0x1000);
            sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(nvic, timer_irq[i]));
            qdev_connect_gpio_out(dev, 0, adc);
        }
    }

    if (DEV_CAP(1, WDT)) {
        dev = qdev_new(TYPE_LUMINARY_WATCHDOG);
        object_property_add_child(soc_container, "wdg", OBJECT(dev));
        qdev_connect_clock_in(dev, "WDOGCLK",
                              qdev_get_clock_out(ssys_dev, "SYSCLK"));

        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev),
                        0,
                        0x40000000u);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev),
                           0,
                           qdev_get_gpio_in(nvic, 18));
    }


    for (i = 0; i < NUM_GPIO; i++) {
        if (DEV_CAP(4, GPIO(i))) {
            gpio_dev[i] = sysbus_create_simple("pl061_luminary", gpio_addr[i],
                                               qdev_get_gpio_in(nvic,
                                                                gpio_irq[i]));
            for (j = 0; j < 8; j++) {
                gpio_in[i][j] = qdev_get_gpio_in(gpio_dev[i], j);
                gpio_out[i][j] = NULL;
            }
        }
    }

    for (i = 0; i < NUM_I2C; i++) {
        if (DEV_CAP(2, I2C(i))) {
            i2c_dev[i] = sysbus_create_simple(TYPE_STELLARIS_I2C, i2c_addr[i],
                                              qdev_get_gpio_in(nvic,
                                                               i2c_irq[i]));
        }
    }
    if (board->peripherals & BP_OLED_I2C) {
        I2CBus *bus = (I2CBus *)qdev_get_child_bus(i2c_dev[0], "i2c");

        i2c_slave_create_simple(bus, "ssd0303", 0x3d);
    }

    for (i = 0; i < NUM_UART; i++) {
        if (DEV_CAP(2, UART(i))) {
            SysBusDevice *sbd;

            dev = qdev_new("pl011_luminary");
            object_property_add_child(soc_container, "uart[*]", OBJECT(dev));
            sbd = SYS_BUS_DEVICE(dev);
            qdev_prop_set_chr(dev, "chardev", serial_hd(i));
            sysbus_realize_and_unref(sbd, &error_fatal);
            sysbus_mmio_map(sbd, 0, 0x4000c000 + i * 0x1000);
            sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(nvic, uart_irq[i]));
        }
    }
    if (DEV_CAP(2, SSI)) {
        dev = sysbus_create_simple("pl022", 0x40008000,
                                   qdev_get_gpio_in(nvic, 7));
        if (board->peripherals & BP_OLED_SSI) {
            void *bus;
            DeviceState *sddev;
            DeviceState *ssddev;
            DriveInfo *dinfo;
            DeviceState *carddev;
            DeviceState *gpio_d_splitter;
            BlockBackend *blk;

            bus = qdev_get_child_bus(dev, "ssi");
            sddev = ssi_create_peripheral(bus, "ssi-sd");

            dinfo = drive_get(IF_SD, 0, 0);
            blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
            carddev = qdev_new(TYPE_SD_CARD_SPI);
            qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
            qdev_realize_and_unref(carddev,
                                   qdev_get_child_bus(sddev, "sd-bus"),
                                   &error_fatal);

            ssddev = qdev_new("ssd0323");
            object_property_add_child(OBJECT(ms), "oled", OBJECT(ssddev));
            qdev_prop_set_uint8(ssddev, "cs", 1);
            qdev_realize_and_unref(ssddev, bus, &error_fatal);

            gpio_d_splitter = qdev_new(TYPE_SPLIT_IRQ);
            object_property_add_child(OBJECT(ms), "splitter",
                                      OBJECT(gpio_d_splitter));
            qdev_prop_set_uint32(gpio_d_splitter, "num-lines", 2);
            qdev_realize_and_unref(gpio_d_splitter, NULL, &error_fatal);
            qdev_connect_gpio_out(
                    gpio_d_splitter, 0,
                    qdev_get_gpio_in_named(sddev, SSI_GPIO_CS, 0));
            qdev_connect_gpio_out(
                    gpio_d_splitter, 1,
                    qdev_get_gpio_in_named(ssddev, SSI_GPIO_CS, 0));
            gpio_out[GPIO_D][0] = qdev_get_gpio_in(gpio_d_splitter, 0);

            gpio_out[GPIO_C][7] = qdev_get_gpio_in(ssddev, 0);

            qemu_irq_raise(gpio_out[GPIO_D][0]);
        }
    }
    if (DEV_CAP(4, EMAC)) {
        DeviceState *enet;

        enet = qdev_new("stellaris_enet");
        object_property_add_child(soc_container, "enet", OBJECT(enet));
        if (nd) {
            qdev_set_nic_properties(enet, nd);
        } else {
            qdev_prop_set_macaddr(enet, "mac", mac.a);
        }

        sysbus_realize_and_unref(SYS_BUS_DEVICE(enet), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(enet), 0, 0x40048000);
        sysbus_connect_irq(SYS_BUS_DEVICE(enet), 0, qdev_get_gpio_in(nvic, 42));
    }
    if (board->peripherals & BP_GAMEPAD) {
        QList *gpad_keycode_list = qlist_new();
        static const int gpad_keycode[5] = {
            Q_KEY_CODE_UP, Q_KEY_CODE_DOWN, Q_KEY_CODE_LEFT,
            Q_KEY_CODE_RIGHT, Q_KEY_CODE_CTRL,
        };
        DeviceState *gpad;

        gpad = qdev_new(TYPE_STELLARIS_GAMEPAD);
        object_property_add_child(OBJECT(ms), "gamepad", OBJECT(gpad));
        for (i = 0; i < ARRAY_SIZE(gpad_keycode); i++) {
            qlist_append_int(gpad_keycode_list, gpad_keycode[i]);
        }
        qdev_prop_set_array(gpad, "keycodes", gpad_keycode_list);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(gpad), &error_fatal);

        qdev_connect_gpio_out(gpad, 0,
                              qemu_irq_invert(gpio_in[GPIO_E][0]));
        qdev_connect_gpio_out(gpad, 1,
                              qemu_irq_invert(gpio_in[GPIO_E][1]));
        qdev_connect_gpio_out(gpad, 2,
                              qemu_irq_invert(gpio_in[GPIO_E][2]));
        qdev_connect_gpio_out(gpad, 3,
                              qemu_irq_invert(gpio_in[GPIO_E][3]));
        qdev_connect_gpio_out(gpad, 4,
                              qemu_irq_invert(gpio_in[GPIO_F][1]));
    }
    for (i = 0; i < 7; i++) {
        if (board->dc4 & (1 << i)) {
            for (j = 0; j < 8; j++) {
                if (gpio_out[i][j]) {
                    qdev_connect_gpio_out(gpio_dev[i], j, gpio_out[i][j]);
                }
            }
        }
    }

    create_unimplemented_device("PWM", 0x40028000, 0x1000);
    create_unimplemented_device("QEI-0", 0x4002c000, 0x1000);
    create_unimplemented_device("QEI-1", 0x4002d000, 0x1000);
    create_unimplemented_device("analogue-comparator", 0x4003c000, 0x1000);
    create_unimplemented_device("hibernation", 0x400fc000, 0x1000);
    create_unimplemented_device("flash-control", 0x400fd000, 0x1000);

    armv7m_load_kernel(ARMV7M(armv7m)->cpu, ms->kernel_filename, 0, flash_size);
}

static void lm3s811evb_init(MachineState *machine)
{
    stellaris_init(machine, &stellaris_boards[0]);
}

static void lm3s6965evb_init(MachineState *machine)
{
    stellaris_init(machine, &stellaris_boards[1]);
}

static void lm3s811evb_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Stellaris LM3S811EVB (Cortex-M3)";
    mc->init = lm3s811evb_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
}

static const TypeInfo lm3s811evb_type = {
    .name = MACHINE_TYPE_NAME("lm3s811evb"),
    .parent = TYPE_MACHINE,
    .class_init = lm3s811evb_class_init,
    .interfaces = arm_machine_interfaces,
};

static void lm3s6965evb_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Stellaris LM3S6965EVB (Cortex-M3)";
    mc->init = lm3s6965evb_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
    mc->auto_create_sdcard = true;
}

static const TypeInfo lm3s6965evb_type = {
    .name = MACHINE_TYPE_NAME("lm3s6965evb"),
    .parent = TYPE_MACHINE,
    .class_init = lm3s6965evb_class_init,
    .interfaces = arm_machine_interfaces,
};

static void stellaris_machine_init(void)
{
    type_register_static(&lm3s811evb_type);
    type_register_static(&lm3s6965evb_type);
}

type_init(stellaris_machine_init)

static void stellaris_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = stellaris_i2c_reset_enter;
    rc->phases.hold = stellaris_i2c_reset_hold;
    rc->phases.exit = stellaris_i2c_reset_exit;
    dc->vmsd = &vmstate_stellaris_i2c;
}

static const TypeInfo stellaris_i2c_info = {
    .name          = TYPE_STELLARIS_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(stellaris_i2c_state),
    .instance_init = stellaris_i2c_init,
    .class_init    = stellaris_i2c_class_init,
};

static void stellaris_adc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = stellaris_adc_reset_hold;
    dc->vmsd = &vmstate_stellaris_adc;
}

static const TypeInfo stellaris_adc_info = {
    .name          = TYPE_STELLARIS_ADC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StellarisADCState),
    .instance_init = stellaris_adc_init,
    .class_init    = stellaris_adc_class_init,
};

static void stellaris_sys_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_stellaris_sys;
    rc->phases.enter = stellaris_sys_reset_enter;
    rc->phases.hold = stellaris_sys_reset_hold;
    rc->phases.exit = stellaris_sys_reset_exit;
    device_class_set_props(dc, stellaris_sys_properties);
}

static const TypeInfo stellaris_sys_info = {
    .name = TYPE_STELLARIS_SYS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ssys_state),
    .instance_init = stellaris_sys_instance_init,
    .class_init = stellaris_sys_class_init,
};

static void stellaris_register_types(void)
{
    type_register_static(&stellaris_i2c_info);
    type_register_static(&stellaris_adc_info);
    type_register_static(&stellaris_sys_info);
}

type_init(stellaris_register_types)
