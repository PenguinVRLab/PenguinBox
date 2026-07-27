/*
 * Allwinner CPU Configuration Module emulation
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
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu.h"
#include "hw/misc/allwinner-cpucfg.h"
#include "trace.h"

enum {
    REG_CPUS_RST_CTRL       = 0x0000,
    REG_CPU0_RST_CTRL       = 0x0040,
    REG_CPU0_CTRL           = 0x0044,
    REG_CPU0_STATUS         = 0x0048,
    REG_CPU1_RST_CTRL       = 0x0080,
    REG_CPU1_CTRL           = 0x0084,
    REG_CPU1_STATUS         = 0x0088,
    REG_CPU2_RST_CTRL       = 0x00C0,
    REG_CPU2_CTRL           = 0x00C4,
    REG_CPU2_STATUS         = 0x00C8,
    REG_CPU3_RST_CTRL       = 0x0100,
    REG_CPU3_CTRL           = 0x0104,
    REG_CPU3_STATUS         = 0x0108,
    REG_CPU_SYS_RST         = 0x0140,
    REG_CLK_GATING          = 0x0144,
    REG_GEN_CTRL            = 0x0184,
    REG_SUPER_STANDBY       = 0x01A0,
    REG_ENTRY_ADDR          = 0x01A4,
    REG_DBG_EXTERN          = 0x01E4,
    REG_CNT64_CTRL          = 0x0280,
    REG_CNT64_LOW           = 0x0284,
    REG_CNT64_HIGH          = 0x0288,
};

enum {
    CPUX_RESET_RELEASED     = ((1 << 1) | (1 << 0)),
    CPUX_STATUS_SMP         = (1 << 0),
    CPU_SYS_RESET_RELEASED  = (1 << 0),
    CLK_GATING_ENABLE       = ((1 << 8) | 0xF),
};

enum {
    REG_CLK_GATING_RST      = 0x0000010F,
    REG_GEN_CTRL_RST        = 0x00000020,
    REG_SUPER_STANDBY_RST   = 0x0,
    REG_CNT64_CTRL_RST      = 0x0,
};

enum {
    CPU_EXCEPTION_LEVEL_ON_RESET = 3,
};

static void allwinner_cpucfg_cpu_reset(AwCpuCfgState *s, uint8_t cpu_id)
{
    int ret;

    trace_allwinner_cpucfg_cpu_reset(cpu_id, s->entry_addr);

    ARMCPU *target_cpu = ARM_CPU(arm_get_cpu_by_id(cpu_id));
    if (!target_cpu) {
        return;
    }
    bool target_aa64 = arm_feature(&target_cpu->env, ARM_FEATURE_AARCH64);

    ret = arm_set_cpu_on(cpu_id, s->entry_addr, 0,
                         CPU_EXCEPTION_LEVEL_ON_RESET, target_aa64);
    if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS) {
        error_report("%s: failed to bring up CPU %d: err %d",
                     __func__, cpu_id, ret);
        return;
    }
}

static uint64_t allwinner_cpucfg_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwCpuCfgState *s = AW_CPUCFG(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_CPUS_RST_CTRL:
    case REG_CPU_SYS_RST:
        val = CPU_SYS_RESET_RELEASED;
        break;
    case REG_CPU0_RST_CTRL:
    case REG_CPU1_RST_CTRL:
    case REG_CPU2_RST_CTRL:
    case REG_CPU3_RST_CTRL:
        val = CPUX_RESET_RELEASED;
        break;
    case REG_CPU0_CTRL:
    case REG_CPU1_CTRL:
    case REG_CPU2_CTRL:
    case REG_CPU3_CTRL:
        val = 0;
        break;
    case REG_CPU0_STATUS:
    case REG_CPU1_STATUS:
    case REG_CPU2_STATUS:
    case REG_CPU3_STATUS:
        val = CPUX_STATUS_SMP;
        break;
    case REG_CLK_GATING:
        val = CLK_GATING_ENABLE;
        break;
    case REG_GEN_CTRL:
        val = s->gen_ctrl;
        break;
    case REG_SUPER_STANDBY:
        val = s->super_standby;
        break;
    case REG_ENTRY_ADDR:
        val = s->entry_addr;
        break;
    case REG_DBG_EXTERN:
    case REG_CNT64_CTRL:
    case REG_CNT64_LOW:
    case REG_CNT64_HIGH:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register at 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    trace_allwinner_cpucfg_read(offset, val, size);

    return val;
}

static void allwinner_cpucfg_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwCpuCfgState *s = AW_CPUCFG(opaque);

    trace_allwinner_cpucfg_write(offset, val, size);

    switch (offset) {
    case REG_CPUS_RST_CTRL:
    case REG_CPU_SYS_RST:
        break;
    case REG_CPU0_RST_CTRL:
    case REG_CPU1_RST_CTRL:
    case REG_CPU2_RST_CTRL:
    case REG_CPU3_RST_CTRL:
        if (val) {
            allwinner_cpucfg_cpu_reset(s, (offset - REG_CPU0_RST_CTRL) >> 6);
        }
        break;
    case REG_CPU0_CTRL:
    case REG_CPU1_CTRL:
    case REG_CPU2_CTRL:
    case REG_CPU3_CTRL:
    case REG_CPU0_STATUS:
    case REG_CPU1_STATUS:
    case REG_CPU2_STATUS:
    case REG_CPU3_STATUS:
    case REG_CLK_GATING:
        break;
    case REG_GEN_CTRL:
        s->gen_ctrl = val;
        break;
    case REG_SUPER_STANDBY:
        s->super_standby = val;
        break;
    case REG_ENTRY_ADDR:
        s->entry_addr = val;
        break;
    case REG_DBG_EXTERN:
    case REG_CNT64_CTRL:
    case REG_CNT64_LOW:
    case REG_CNT64_HIGH:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register at 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }
}

static const MemoryRegionOps allwinner_cpucfg_ops = {
    .read = allwinner_cpucfg_read,
    .write = allwinner_cpucfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_cpucfg_reset(DeviceState *dev)
{
    AwCpuCfgState *s = AW_CPUCFG(dev);

    s->gen_ctrl = REG_GEN_CTRL_RST;
    s->super_standby = REG_SUPER_STANDBY_RST;
    s->entry_addr = 0;
}

static void allwinner_cpucfg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwCpuCfgState *s = AW_CPUCFG(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_cpucfg_ops, s,
                          TYPE_AW_CPUCFG, 1 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_cpucfg_vmstate = {
    .name = "allwinner-cpucfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(gen_ctrl, AwCpuCfgState),
        VMSTATE_UINT32(super_standby, AwCpuCfgState),
        VMSTATE_UINT32(entry_addr, AwCpuCfgState),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_cpucfg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_cpucfg_reset);
    dc->vmsd = &allwinner_cpucfg_vmstate;
}

static const TypeInfo allwinner_cpucfg_info = {
    .name          = TYPE_AW_CPUCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_cpucfg_init,
    .instance_size = sizeof(AwCpuCfgState),
    .class_init    = allwinner_cpucfg_class_init,
};

static void allwinner_cpucfg_register(void)
{
    type_register_static(&allwinner_cpucfg_info);
}

type_init(allwinner_cpucfg_register)
