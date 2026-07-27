/*
 * Status and system control registers for ARM RealView/Versatile boards.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"
#include "system/runstate.h"
#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/arm/primecell.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define LOCK_VALUE 0xa05f

#define TYPE_ARM_SYSCTL "realview_sysctl"
OBJECT_DECLARE_SIMPLE_TYPE(arm_sysctl_state, ARM_SYSCTL)

struct arm_sysctl_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq pl110_mux_ctrl;

    uint32_t sys_id;
    uint32_t leds;
    uint16_t lockval;
    uint32_t cfgdata1;
    uint32_t cfgdata2;
    uint32_t flags;
    uint32_t nvflags;
    uint32_t resetlevel;
    uint32_t proc_id;
    uint32_t sys_mci;
    uint32_t sys_cfgdata;
    uint32_t sys_cfgctrl;
    uint32_t sys_cfgstat;
    uint32_t sys_clcd;
    uint32_t mb_clock[6];
    uint32_t *db_clock;
    uint32_t db_num_vsensors;
    uint32_t *db_voltage;
    uint32_t db_num_clocks;
    uint32_t *db_clock_reset;
};

static const VMStateDescription vmstate_arm_sysctl = {
    .name = "realview_sysctl",
    .version_id = 4,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(leds, arm_sysctl_state),
        VMSTATE_UINT16(lockval, arm_sysctl_state),
        VMSTATE_UINT32(cfgdata1, arm_sysctl_state),
        VMSTATE_UINT32(cfgdata2, arm_sysctl_state),
        VMSTATE_UINT32(flags, arm_sysctl_state),
        VMSTATE_UINT32(nvflags, arm_sysctl_state),
        VMSTATE_UINT32(resetlevel, arm_sysctl_state),
        VMSTATE_UINT32_V(sys_mci, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_cfgdata, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_cfgctrl, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_cfgstat, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_clcd, arm_sysctl_state, 3),
        VMSTATE_UINT32_ARRAY_V(mb_clock, arm_sysctl_state, 6, 4),
        VMSTATE_VARRAY_UINT32(db_clock, arm_sysctl_state, db_num_clocks,
                              4, vmstate_info_uint32, uint32_t),
        VMSTATE_END_OF_LIST()
    }
};

#define BOARD_ID_PB926 0x100
#define BOARD_ID_EB 0x140
#define BOARD_ID_PBA8 0x178
#define BOARD_ID_PBX 0x182
#define BOARD_ID_VEXPRESS 0x190

static int board_id(arm_sysctl_state *s)
{
    return (s->sys_id >> 16) & 0xfff;
}

static void arm_sysctl_reset(DeviceState *d)
{
    arm_sysctl_state *s = ARM_SYSCTL(d);
    int i;

    s->leds = 0;
    s->lockval = 0;
    s->cfgdata1 = 0;
    s->cfgdata2 = 0;
    s->flags = 0;
    s->resetlevel = 0;
    s->mb_clock[0] = 50000000;
    s->mb_clock[1] = 23750000;
    s->mb_clock[2] = 24000000;
    s->mb_clock[3] = 24000000;
    s->mb_clock[4] = 24000000;
    s->mb_clock[5] = 24000000;
    for (i = 0; i < s->db_num_clocks; i++) {
        s->db_clock[i] = s->db_clock_reset[i];
    }
    if (board_id(s) == BOARD_ID_VEXPRESS) {
        s->sys_clcd = 0;
    } else {
        s->sys_clcd = 0x1f00;
    }
}

static uint64_t arm_sysctl_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;

    switch (offset) {
    case 0x00:
        return s->sys_id;
    case 0x04:
        return 0;
    case 0x08:
        return s->leds;
    case 0x20:
        return s->lockval;
    case 0x0c:
    case 0x10:
    case 0x14:
    case 0x18:
    case 0x1c:
    case 0x24:
        return 0;
    case 0x28:
        return s->cfgdata1;
    case 0x2c:
        return s->cfgdata2;
    case 0x30:
        return s->flags;
    case 0x38:
        return s->nvflags;
    case 0x40:
        if (board_id(s) == BOARD_ID_VEXPRESS) {
            return 0;
        }
        return s->resetlevel;
    case 0x44:
        return 1;
    case 0x48:
        return s->sys_mci;
    case 0x4c:
        return 0;
    case 0x50:
        return s->sys_clcd;
    case 0x54:
        return 0;
    case 0x58:
        return 0;
    case 0x5c:
        return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), 24000000,
                        NANOSECONDS_PER_SECOND);
    case 0x60:
        return 0;
    case 0x84:
        return s->proc_id;
    case 0x88:
        return 0xff000000;
    case 0x64:
    case 0x68:
    case 0x6c:
    case 0x70:
    case 0x74:
    case 0x80:
    case 0x8c:
    case 0x90:
    case 0x94:
    case 0x98:
    case 0x9c:
    case 0xc0:
    case 0xc4:
    case 0xc8:
    case 0xcc:
    case 0xd0:
        return 0;
    case 0xa0:
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        return s->sys_cfgdata;
    case 0xa4:
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        return s->sys_cfgctrl;
    case 0xa8:
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        return s->sys_cfgstat;
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "arm_sysctl_read: Bad register offset 0x%x\n",
                      (int)offset);
        return 0;
    }
}

#define SYS_CFG_OSC 1
#define SYS_CFG_VOLT 2
#define SYS_CFG_AMP 3
#define SYS_CFG_TEMP 4
#define SYS_CFG_RESET 5
#define SYS_CFG_SCC 6
#define SYS_CFG_MUXFPGA 7
#define SYS_CFG_SHUTDOWN 8
#define SYS_CFG_REBOOT 9
#define SYS_CFG_DVIMODE 11
#define SYS_CFG_POWER 12
#define SYS_CFG_ENERGY 13

#define SYS_CFG_SITE_MB 0
#define SYS_CFG_SITE_DB1 1
#define SYS_CFG_SITE_DB2 2

static bool vexpress_cfgctrl_read(arm_sysctl_state *s, unsigned int dcc,
                                  unsigned int function, unsigned int site,
                                  unsigned int position, unsigned int device,
                                  uint32_t *val)
{
    if (dcc != 0 || position != 0 ||
        (site != SYS_CFG_SITE_MB && site != SYS_CFG_SITE_DB1)) {
        goto cfgctrl_unimp;
    }

    switch (function) {
    case SYS_CFG_VOLT:
        if (site == SYS_CFG_SITE_DB1 && device < s->db_num_vsensors) {
            *val = s->db_voltage[device];
            return true;
        }
        if (site == SYS_CFG_SITE_MB && device == 0) {
            *val = 3300000;
            return true;
        }
        break;
    case SYS_CFG_OSC:
        if (site == SYS_CFG_SITE_MB && device < ARRAY_SIZE(s->mb_clock)) {
            *val = s->mb_clock[device];
            return true;
        }
        if (site == SYS_CFG_SITE_DB1 && device < s->db_num_clocks) {
            *val = s->db_clock[device];
            return true;
        }
        break;
    default:
        break;
    }

cfgctrl_unimp:
    qemu_log_mask(LOG_UNIMP,
                  "arm_sysctl: Unimplemented SYS_CFGCTRL read of function "
                  "0x%x DCC 0x%x site 0x%x position 0x%x device 0x%x\n",
                  function, dcc, site, position, device);
    return false;
}

static bool vexpress_cfgctrl_write(arm_sysctl_state *s, unsigned int dcc,
                                   unsigned int function, unsigned int site,
                                   unsigned int position, unsigned int device,
                                   uint32_t val)
{
    if (dcc != 0 || position != 0 ||
        (site != SYS_CFG_SITE_MB && site != SYS_CFG_SITE_DB1)) {
        goto cfgctrl_unimp;
    }

    switch (function) {
    case SYS_CFG_OSC:
        if (site == SYS_CFG_SITE_MB && device < ARRAY_SIZE(s->mb_clock)) {
            s->mb_clock[device] = val;
            return true;
        }
        if (site == SYS_CFG_SITE_DB1 && device < s->db_num_clocks) {
            s->db_clock[device] = val;
            return true;
        }
        break;
    case SYS_CFG_MUXFPGA:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            qemu_log_mask(LOG_UNIMP, "arm_sysctl: selection of video output "
                          "not supported, ignoring\n");
            return true;
        }
        break;
    case SYS_CFG_SHUTDOWN:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            return true;
        }
        break;
    case SYS_CFG_REBOOT:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return true;
        }
        break;
    case SYS_CFG_DVIMODE:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            return true;
        }
    default:
        break;
    }

cfgctrl_unimp:
    qemu_log_mask(LOG_UNIMP,
                  "arm_sysctl: Unimplemented SYS_CFGCTRL write of function "
                  "0x%x DCC 0x%x site 0x%x position 0x%x device 0x%x\n",
                  function, dcc, site, position, device);
    return false;
}

static void arm_sysctl_write(void *opaque, hwaddr offset,
                             uint64_t val, unsigned size)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;

    switch (offset) {
    case 0x08:
        s->leds = val;
        break;
    case 0x0c:
    case 0x10:
    case 0x14:
    case 0x18:
    case 0x1c:
        break;
    case 0x20:
        if (val == LOCK_VALUE)
            s->lockval = val;
        else
            s->lockval = val & 0x7fff;
        break;
    case 0x28:
        s->cfgdata1 = val;
        break;
    case 0x2c:
        s->cfgdata2 = val;
        break;
    case 0x30:
        s->flags |= val;
        break;
    case 0x34:
        s->flags &= ~val;
        break;
    case 0x38:
        s->nvflags |= val;
        break;
    case 0x3c:
        s->nvflags &= ~val;
        break;
    case 0x40:
        switch (board_id(s)) {
        case BOARD_ID_PB926:
            if (s->lockval == LOCK_VALUE) {
                s->resetlevel = val;
                if (val & 0x100) {
                    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                }
            }
            break;
        case BOARD_ID_PBX:
        case BOARD_ID_PBA8:
            if (s->lockval == LOCK_VALUE) {
                s->resetlevel = val;
                if (val & 0x04) {
                    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                }
            }
            break;
        case BOARD_ID_VEXPRESS:
        case BOARD_ID_EB:
        default:
            break;
        }
        break;
    case 0x44:
        break;
    case 0x4c:
        break;
    case 0x50:
        switch (board_id(s)) {
        case BOARD_ID_PB926:
            s->sys_clcd &= 0x3f00;
            s->sys_clcd |= val & ~0x3f00;
            qemu_set_irq(s->pl110_mux_ctrl, val & 3);
            break;
        case BOARD_ID_EB:
            s->sys_clcd &= 0x3f00;
            s->sys_clcd |= val & ~0x3f00;
            break;
        case BOARD_ID_PBA8:
        case BOARD_ID_PBX:
            s->sys_clcd &= (1 << 7);
            s->sys_clcd |= val & ~(1 << 7);
            break;
        case BOARD_ID_VEXPRESS:
        default:
            break;
        }
        break;
    case 0x54:
    case 0x64:
    case 0x68:
    case 0x6c:
    case 0x70:
    case 0x74:
    case 0x80:
    case 0x84:
    case 0x88:
    case 0x8c:
    case 0x90:
    case 0x94:
    case 0x98:
    case 0x9c:
        break;
    case 0xa0:
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgdata = val;
        return;
    case 0xa4:
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgctrl = val & ~((3 << 18) | (1 << 31));
        if (extract64(val, 31, 1)) {
            unsigned int dcc = extract32(s->sys_cfgctrl, 26, 4);
            unsigned int function = extract32(s->sys_cfgctrl, 20, 6);
            unsigned int site = extract32(s->sys_cfgctrl, 16, 2);
            unsigned int position = extract32(s->sys_cfgctrl, 12, 4);
            unsigned int device = extract32(s->sys_cfgctrl, 0, 12);
            s->sys_cfgstat = 1;
            if (s->sys_cfgctrl & (1 << 30)) {
                if (!vexpress_cfgctrl_write(s, dcc, function, site, position,
                                            device, s->sys_cfgdata)) {
                    s->sys_cfgstat |= 2;
                }
            } else {
                uint32_t data;
                if (!vexpress_cfgctrl_read(s, dcc, function, site, position,
                                           device, &data)) {
                    s->sys_cfgstat |= 2;
                } else {
                    s->sys_cfgdata = data;
                }
            }
        }
        s->sys_cfgctrl &= ~(1 << 31);
        return;
    case 0xa8:
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgstat = val & 3;
        return;
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "arm_sysctl_write: Bad register offset 0x%x\n",
                      (int)offset);
        return;
    }
}

static const MemoryRegionOps arm_sysctl_ops = {
    .read = arm_sysctl_read,
    .write = arm_sysctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void arm_sysctl_gpio_set(void *opaque, int line, int level)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;
    switch (line) {
    case ARM_SYSCTL_GPIO_MMC_WPROT:
    {
        int bit = 2;
        if ((board_id(s) == BOARD_ID_PB926) || (board_id(s) == BOARD_ID_EB)) {
            bit = 4;
        }
        s->sys_mci &= ~bit;
        if (level) {
            s->sys_mci |= bit;
        }
        break;
    }
    case ARM_SYSCTL_GPIO_MMC_CARDIN:
        s->sys_mci &= ~1;
        if (level) {
            s->sys_mci |= 1;
        }
        break;
    }
}

static void arm_sysctl_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    arm_sysctl_state *s = ARM_SYSCTL(obj);

    memory_region_init_io(&s->iomem, OBJECT(dev), &arm_sysctl_ops, s,
                          "arm-sysctl", 0x1000);
    sysbus_init_mmio(sd, &s->iomem);
    qdev_init_gpio_in(dev, arm_sysctl_gpio_set, 2);
    qdev_init_gpio_out(dev, &s->pl110_mux_ctrl, 1);
}

static void arm_sysctl_realize(DeviceState *d, Error **errp)
{
    arm_sysctl_state *s = ARM_SYSCTL(d);

    s->db_clock = g_new0(uint32_t, s->db_num_clocks);
}

static void arm_sysctl_finalize(Object *obj)
{
    arm_sysctl_state *s = ARM_SYSCTL(obj);

    g_free(s->db_voltage);
    g_free(s->db_clock);
    g_free(s->db_clock_reset);
}

static const Property arm_sysctl_properties[] = {
    DEFINE_PROP_UINT32("sys_id", arm_sysctl_state, sys_id, 0),
    DEFINE_PROP_UINT32("proc_id", arm_sysctl_state, proc_id, 0),
    DEFINE_PROP_ARRAY("db-voltage", arm_sysctl_state, db_num_vsensors,
                      db_voltage, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_ARRAY("db-clock", arm_sysctl_state, db_num_clocks,
                      db_clock_reset, qdev_prop_uint32, uint32_t),
};

static void arm_sysctl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = arm_sysctl_realize;
    device_class_set_legacy_reset(dc, arm_sysctl_reset);
    dc->vmsd = &vmstate_arm_sysctl;
    device_class_set_props(dc, arm_sysctl_properties);
}

static const TypeInfo arm_sysctl_info = {
    .name          = TYPE_ARM_SYSCTL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(arm_sysctl_state),
    .instance_init = arm_sysctl_init,
    .instance_finalize = arm_sysctl_finalize,
    .class_init    = arm_sysctl_class_init,
};

static void arm_sysctl_register_types(void)
{
    type_register_static(&arm_sysctl_info);
}

type_init(arm_sysctl_register_types)
