/*
 * Arm SSE (Subsystems for Embedded): IoTKit
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/arm/armsse.h"
#include "hw/arm/armsse-version.h"
#include "hw/arm/boot.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"

#define NO_IRQ -1
#define NO_PPC -1
#define NMI_0 10000
#define NMI_1 10001

typedef struct ARMSSEDeviceInfo {
    const char *name;
    const char *type;
    unsigned int index;
    hwaddr addr;
    hwaddr size;
    int ppc;
    int ppc_port;
    int irq;
    bool slowclk;
} ARMSSEDeviceInfo;

struct ARMSSEInfo {
    const char *name;
    const char *cpu_type;
    uint32_t sse_version;
    int sram_banks;
    uint32_t sram_bank_base;
    int num_cpus;
    uint32_t sys_version;
    uint32_t iidr;
    uint32_t cpuwait_rst;
    bool has_mhus;
    bool has_cachectrl;
    bool has_cpusecctrl;
    bool has_cpuid;
    bool has_cpu_pwrctrl;
    bool has_sse_counter;
    bool has_tcms;
    uint8_t props_count;
    const Property *props;
    const ARMSSEDeviceInfo *devinfo;
    const bool *irq_is_common;
};

static const Property iotkit_properties[] = {
    DEFINE_PROP_LINK("memory", ARMSSE, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("EXP_NUMIRQ", ARMSSE, exp_numirq, 64),
    DEFINE_PROP_UINT32("SRAM_ADDR_WIDTH", ARMSSE, sram_addr_width, 15),
    DEFINE_PROP_UINT32("init-svtor", ARMSSE, init_svtor, 0x10000000),
    DEFINE_PROP_BOOL("CPU0_FPU", ARMSSE, cpu_fpu[0], true),
    DEFINE_PROP_BOOL("CPU0_DSP", ARMSSE, cpu_dsp[0], true),
    DEFINE_PROP_UINT32("CPU0_MPU_NS", ARMSSE, cpu_mpu_ns[0], 8),
    DEFINE_PROP_UINT32("CPU0_MPU_S", ARMSSE, cpu_mpu_s[0], 8),
};

static const Property sse200_properties[] = {
    DEFINE_PROP_LINK("memory", ARMSSE, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("EXP_NUMIRQ", ARMSSE, exp_numirq, 64),
    DEFINE_PROP_UINT32("SRAM_ADDR_WIDTH", ARMSSE, sram_addr_width, 15),
    DEFINE_PROP_UINT32("init-svtor", ARMSSE, init_svtor, 0x10000000),
    DEFINE_PROP_BOOL("CPU0_FPU", ARMSSE, cpu_fpu[0], false),
    DEFINE_PROP_BOOL("CPU0_DSP", ARMSSE, cpu_dsp[0], false),
    DEFINE_PROP_BOOL("CPU1_FPU", ARMSSE, cpu_fpu[1], true),
    DEFINE_PROP_BOOL("CPU1_DSP", ARMSSE, cpu_dsp[1], true),
    DEFINE_PROP_UINT32("CPU0_MPU_NS", ARMSSE, cpu_mpu_ns[0], 8),
    DEFINE_PROP_UINT32("CPU0_MPU_S", ARMSSE, cpu_mpu_s[0], 8),
    DEFINE_PROP_UINT32("CPU1_MPU_NS", ARMSSE, cpu_mpu_ns[1], 8),
    DEFINE_PROP_UINT32("CPU1_MPU_S", ARMSSE, cpu_mpu_s[1], 8),
};

static const Property sse300_properties[] = {
    DEFINE_PROP_LINK("memory", ARMSSE, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("EXP_NUMIRQ", ARMSSE, exp_numirq, 64),
    DEFINE_PROP_UINT32("SRAM_ADDR_WIDTH", ARMSSE, sram_addr_width, 18),
    DEFINE_PROP_UINT32("init-svtor", ARMSSE, init_svtor, 0x10000000),
    DEFINE_PROP_BOOL("CPU0_FPU", ARMSSE, cpu_fpu[0], true),
    DEFINE_PROP_BOOL("CPU0_DSP", ARMSSE, cpu_dsp[0], true),
    DEFINE_PROP_UINT32("CPU0_MPU_NS", ARMSSE, cpu_mpu_ns[0], 8),
    DEFINE_PROP_UINT32("CPU0_MPU_S", ARMSSE, cpu_mpu_s[0], 8),
};

static const ARMSSEDeviceInfo iotkit_devices[] = {
    {
        .name = "timer0",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 0,
        .addr = 0x40000000,
        .ppc = 0,
        .ppc_port = 0,
        .irq = 3,
    },
    {
        .name = "timer1",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 1,
        .addr = 0x40001000,
        .ppc = 0,
        .ppc_port = 1,
        .irq = 4,
    },
    {
        .name = "s32ktimer",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 2,
        .addr = 0x4002f000,
        .ppc = 1,
        .ppc_port = 0,
        .irq = 2,
        .slowclk = true,
    },
    {
        .name = "dualtimer",
        .type = TYPE_CMSDK_APB_DUALTIMER,
        .index = 0,
        .addr = 0x40002000,
        .ppc = 0,
        .ppc_port = 2,
        .irq = 5,
    },
    {
        .name = "s32kwatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 0,
        .addr = 0x5002e000,
        .ppc = NO_PPC,
        .irq = NMI_0,
        .slowclk = true,
    },
    {
        .name = "nswatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 1,
        .addr = 0x40081000,
        .ppc = NO_PPC,
        .irq = 1,
    },
    {
        .name = "swatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 2,
        .addr = 0x50081000,
        .ppc = NO_PPC,
        .irq = NMI_1,
    },
    {
        .name = "armsse-sysinfo",
        .type = TYPE_IOTKIT_SYSINFO,
        .index = 0,
        .addr = 0x40020000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "armsse-sysctl",
        .type = TYPE_IOTKIT_SYSCTL,
        .index = 0,
        .addr = 0x50021000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = NULL,
    }
};

static const ARMSSEDeviceInfo sse200_devices[] = {
    {
        .name = "timer0",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 0,
        .addr = 0x40000000,
        .ppc = 0,
        .ppc_port = 0,
        .irq = 3,
    },
    {
        .name = "timer1",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 1,
        .addr = 0x40001000,
        .ppc = 0,
        .ppc_port = 1,
        .irq = 4,
    },
    {
        .name = "s32ktimer",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 2,
        .addr = 0x4002f000,
        .ppc = 1,
        .ppc_port = 0,
        .irq = 2,
        .slowclk = true,
    },
    {
        .name = "dualtimer",
        .type = TYPE_CMSDK_APB_DUALTIMER,
        .index = 0,
        .addr = 0x40002000,
        .ppc = 0,
        .ppc_port = 2,
        .irq = 5,
    },
    {
        .name = "s32kwatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 0,
        .addr = 0x5002e000,
        .ppc = NO_PPC,
        .irq = NMI_0,
        .slowclk = true,
    },
    {
        .name = "nswatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 1,
        .addr = 0x40081000,
        .ppc = NO_PPC,
        .irq = 1,
    },
    {
        .name = "swatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 2,
        .addr = 0x50081000,
        .ppc = NO_PPC,
        .irq = NMI_1,
    },
    {
        .name = "armsse-sysinfo",
        .type = TYPE_IOTKIT_SYSINFO,
        .index = 0,
        .addr = 0x40020000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "armsse-sysctl",
        .type = TYPE_IOTKIT_SYSCTL,
        .index = 0,
        .addr = 0x50021000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "CPU0CORE_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 0,
        .addr = 0x50023000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "CPU1CORE_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 1,
        .addr = 0x50025000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "DBG_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 2,
        .addr = 0x50029000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "RAM0_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 3,
        .addr = 0x5002a000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "RAM1_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 4,
        .addr = 0x5002b000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "RAM2_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 5,
        .addr = 0x5002c000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "RAM3_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 6,
        .addr = 0x5002d000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "SYS_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 7,
        .addr = 0x50022000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = NULL,
    }
};

static const ARMSSEDeviceInfo sse300_devices[] = {
    {
        .name = "timer0",
        .type = TYPE_SSE_TIMER,
        .index = 0,
        .addr = 0x48000000,
        .ppc = 0,
        .ppc_port = 0,
        .irq = 3,
    },
    {
        .name = "timer1",
        .type = TYPE_SSE_TIMER,
        .index = 1,
        .addr = 0x48001000,
        .ppc = 0,
        .ppc_port = 1,
        .irq = 4,
    },
    {
        .name = "timer2",
        .type = TYPE_SSE_TIMER,
        .index = 2,
        .addr = 0x48002000,
        .ppc = 0,
        .ppc_port = 2,
        .irq = 5,
    },
    {
        .name = "timer3",
        .type = TYPE_SSE_TIMER,
        .index = 3,
        .addr = 0x48003000,
        .ppc = 0,
        .ppc_port = 5,
        .irq = 27,
    },
    {
        .name = "s32ktimer",
        .type = TYPE_CMSDK_APB_TIMER,
        .index = 0,
        .addr = 0x4802f000,
        .ppc = 1,
        .ppc_port = 0,
        .irq = 2,
        .slowclk = true,
    },
    {
        .name = "s32kwatchdog",
        .type = TYPE_CMSDK_APB_WATCHDOG,
        .index = 0,
        .addr = 0x4802e000,
        .ppc = NO_PPC,
        .irq = NMI_0,
        .slowclk = true,
    },
    {
        .name = "watchdog",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 0,
        .addr = 0x48040000,
        .size = 0x2000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "armsse-sysinfo",
        .type = TYPE_IOTKIT_SYSINFO,
        .index = 0,
        .addr = 0x48020000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "armsse-sysctl",
        .type = TYPE_IOTKIT_SYSCTL,
        .index = 0,
        .addr = 0x58021000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "SYS_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 1,
        .addr = 0x58022000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "CPU0CORE_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 2,
        .addr = 0x50023000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "MGMT_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 3,
        .addr = 0x50028000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = "DEBUG_PPU",
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .index = 4,
        .addr = 0x50029000,
        .size = 0x1000,
        .ppc = NO_PPC,
        .irq = NO_IRQ,
    },
    {
        .name = NULL,
    }
};

static const bool sse200_irq_is_common[32] = {
    [0 ... 5] = true,
    [8 ... 12] = true,
    [15 ... 20] = true,
    [22 ... 26] = true,
};

static const bool sse300_irq_is_common[32] = {
    [0 ... 5] = true,
    [8 ... 12] = true,
    [14 ... 16] = true,
    [26 ... 27] = true,
};

static const ARMSSEInfo armsse_variants[] = {
    {
        .name = TYPE_IOTKIT,
        .sse_version = ARMSSE_IOTKIT,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-m33"),
        .sram_banks = 1,
        .sram_bank_base = 0x20000000,
        .num_cpus = 1,
        .sys_version = 0x41743,
        .iidr = 0,
        .cpuwait_rst = 0,
        .has_mhus = false,
        .has_cachectrl = false,
        .has_cpusecctrl = false,
        .has_cpuid = false,
        .has_cpu_pwrctrl = false,
        .has_sse_counter = false,
        .has_tcms = false,
        .props = iotkit_properties,
        .props_count = ARRAY_SIZE(iotkit_properties),
        .devinfo = iotkit_devices,
        .irq_is_common = sse200_irq_is_common,
    },
    {
        .name = TYPE_SSE200,
        .sse_version = ARMSSE_SSE200,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-m33"),
        .sram_banks = 4,
        .sram_bank_base = 0x20000000,
        .num_cpus = 2,
        .sys_version = 0x22041743,
        .iidr = 0,
        .cpuwait_rst = 2,
        .has_mhus = true,
        .has_cachectrl = true,
        .has_cpusecctrl = true,
        .has_cpuid = true,
        .has_cpu_pwrctrl = false,
        .has_sse_counter = false,
        .has_tcms = false,
        .props = sse200_properties,
        .props_count = ARRAY_SIZE(sse200_properties),
        .devinfo = sse200_devices,
        .irq_is_common = sse200_irq_is_common,
    },
    {
        .name = TYPE_SSE300,
        .sse_version = ARMSSE_SSE300,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-m55"),
        .sram_banks = 2,
        .sram_bank_base = 0x21000000,
        .num_cpus = 1,
        .sys_version = 0x7e00043b,
        .iidr = 0x74a0043b,
        .cpuwait_rst = 0,
        .has_mhus = false,
        .has_cachectrl = false,
        .has_cpusecctrl = true,
        .has_cpuid = true,
        .has_cpu_pwrctrl = true,
        .has_sse_counter = true,
        .has_tcms = true,
        .props = sse300_properties,
        .props_count = ARRAY_SIZE(sse300_properties),
        .devinfo = sse300_devices,
        .irq_is_common = sse300_irq_is_common,
    },
};

static uint32_t armsse_sys_config_value(ARMSSE *s, const ARMSSEInfo *info)
{
    uint32_t sys_config;

    switch (info->sse_version) {
    case ARMSSE_IOTKIT:
        sys_config = 0;
        sys_config = deposit32(sys_config, 0, 4, info->sram_banks);
        sys_config = deposit32(sys_config, 4, 4, s->sram_addr_width - 12);
        break;
    case ARMSSE_SSE200:
        sys_config = 0;
        sys_config = deposit32(sys_config, 0, 4, info->sram_banks);
        sys_config = deposit32(sys_config, 4, 5, s->sram_addr_width);
        sys_config = deposit32(sys_config, 24, 4, 2);
        if (info->num_cpus > 1) {
            sys_config = deposit32(sys_config, 10, 1, 1);
            sys_config = deposit32(sys_config, 20, 4, info->sram_banks - 1);
            sys_config = deposit32(sys_config, 28, 4, 2);
        }
        break;
    case ARMSSE_SSE300:
        sys_config = 0;
        sys_config = deposit32(sys_config, 0, 4, info->sram_banks);
        sys_config = deposit32(sys_config, 4, 5, s->sram_addr_width);
        sys_config = deposit32(sys_config, 16, 3, 3);
        break;
    default:
        g_assert_not_reached();
    }
    return sys_config;
}

#define S32KCLK (32 * 1000)

static void make_alias(ARMSSE *s, MemoryRegion *mr, MemoryRegion *container,
                       const char *name, hwaddr base, hwaddr size, hwaddr orig)
{
    memory_region_init_alias(mr, NULL, name, container, orig, size);
    memory_region_add_subregion_overlap(container, base, mr, -1500);
}

static void irq_status_forwarder(void *opaque, int n, int level)
{
    qemu_irq destirq = opaque;

    qemu_set_irq(destirq, level);
}

static void nsccfg_handler(void *opaque, int n, int level)
{
    ARMSSE *s = ARM_SSE(opaque);

    s->nsccfg = level;
}

static void armsse_forward_ppc(ARMSSE *s, const char *ppcname, int ppcnum)
{
    SplitIRQ *splitter = &s->ppc_irq_splitter[ppcnum];
    DeviceState *armssedev = DEVICE(s);
    DeviceState *dev_secctl = DEVICE(&s->secctl);
    DeviceState *dev_splitter = DEVICE(splitter);
    char *name;

    name = g_strdup_printf("%s_nonsec", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);
    name = g_strdup_printf("%s_ap", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);
    name = g_strdup_printf("%s_irq_enable", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);
    name = g_strdup_printf("%s_irq_clear", ppcname);
    qdev_pass_gpios(dev_secctl, armssedev, name);
    g_free(name);

    name = g_strdup_printf("%s_irq_status", ppcname);
    qdev_connect_gpio_out(dev_splitter, 0,
                          qdev_get_gpio_in_named(dev_secctl,
                                                 name, 0));
    qdev_connect_gpio_out(dev_splitter, 1,
                          qdev_get_gpio_in(DEVICE(&s->ppc_irq_orgate), ppcnum));
    s->irq_status_in[ppcnum] = qdev_get_gpio_in(dev_splitter, 0);
    qdev_init_gpio_in_named_with_opaque(armssedev, irq_status_forwarder,
                                        s->irq_status_in[ppcnum], name, 1);
    g_free(name);
}

static void armsse_forward_sec_resp_cfg(ARMSSE *s)
{
    DeviceState *dev = DEVICE(s);
    DeviceState *dev_splitter = DEVICE(&s->sec_resp_splitter);

    qdev_init_gpio_out_named(dev, &s->sec_resp_cfg, "sec_resp_cfg", 1);
    s->sec_resp_cfg_in = qemu_allocate_irq(irq_status_forwarder,
                                           s->sec_resp_cfg, 1);
    qdev_connect_gpio_out(dev_splitter, 2, s->sec_resp_cfg_in);
}

static void armsse_init(Object *obj)
{
    ARMSSE *s = ARM_SSE(obj);
    ARMSSEClass *asc = ARM_SSE_GET_CLASS(obj);
    const ARMSSEInfo *info = asc->info;
    const ARMSSEDeviceInfo *devinfo;
    int i;

    assert(info->sram_banks <= MAX_SRAM_BANKS);
    assert(info->num_cpus <= SSE_MAX_CPUS);

    s->mainclk = qdev_init_clock_in(DEVICE(s), "MAINCLK", NULL, NULL, 0);
    s->s32kclk = qdev_init_clock_in(DEVICE(s), "S32KCLK", NULL, NULL, 0);

    memory_region_init(&s->container, obj, "armsse-container", UINT64_MAX);

    for (i = 0; i < info->num_cpus; i++) {
        char *name;

        name = g_strdup_printf("cluster%d", i);
        object_initialize_child(obj, name, &s->cluster[i], TYPE_CPU_CLUSTER);
        qdev_prop_set_uint32(DEVICE(&s->cluster[i]), "cluster-id", i);
        g_free(name);

        name = g_strdup_printf("armv7m%d", i);
        object_initialize_child(OBJECT(&s->cluster[i]), name, &s->armv7m[i],
                                TYPE_ARMV7M);
        qdev_prop_set_string(DEVICE(&s->armv7m[i]), "cpu-type", info->cpu_type);
        g_free(name);
        name = g_strdup_printf("arm-sse-cpu-container%d", i);
        memory_region_init(&s->cpu_container[i], obj, name, UINT64_MAX);
        g_free(name);
        if (i > 0) {
            name = g_strdup_printf("arm-sse-container-alias%d", i);
            memory_region_init_alias(&s->container_alias[i - 1], obj,
                                     name, &s->container, 0, UINT64_MAX);
            g_free(name);
        }
    }

    for (devinfo = info->devinfo; devinfo->name; devinfo++) {
        assert(devinfo->ppc == NO_PPC || devinfo->ppc < ARRAY_SIZE(s->apb_ppc));
        if (!strcmp(devinfo->type, TYPE_CMSDK_APB_TIMER)) {
            assert(devinfo->index < ARRAY_SIZE(s->timer));
            object_initialize_child(obj, devinfo->name,
                                    &s->timer[devinfo->index],
                                    TYPE_CMSDK_APB_TIMER);
        } else if (!strcmp(devinfo->type, TYPE_CMSDK_APB_DUALTIMER)) {
            assert(devinfo->index == 0);
            object_initialize_child(obj, devinfo->name, &s->dualtimer,
                                    TYPE_CMSDK_APB_DUALTIMER);
        } else if (!strcmp(devinfo->type, TYPE_SSE_TIMER)) {
            assert(devinfo->index < ARRAY_SIZE(s->sse_timer));
            object_initialize_child(obj, devinfo->name,
                                    &s->sse_timer[devinfo->index],
                                    TYPE_SSE_TIMER);
        } else if (!strcmp(devinfo->type, TYPE_CMSDK_APB_WATCHDOG)) {
            assert(devinfo->index < ARRAY_SIZE(s->cmsdk_watchdog));
            object_initialize_child(obj, devinfo->name,
                                    &s->cmsdk_watchdog[devinfo->index],
                                    TYPE_CMSDK_APB_WATCHDOG);
        } else if (!strcmp(devinfo->type, TYPE_IOTKIT_SYSINFO)) {
            assert(devinfo->index == 0);
            object_initialize_child(obj, devinfo->name, &s->sysinfo,
                                    TYPE_IOTKIT_SYSINFO);
        } else if (!strcmp(devinfo->type, TYPE_IOTKIT_SYSCTL)) {
            assert(devinfo->index == 0);
            object_initialize_child(obj, devinfo->name, &s->sysctl,
                                    TYPE_IOTKIT_SYSCTL);
        } else if (!strcmp(devinfo->type, TYPE_UNIMPLEMENTED_DEVICE)) {
            assert(devinfo->index < ARRAY_SIZE(s->unimp));
            object_initialize_child(obj, devinfo->name,
                                    &s->unimp[devinfo->index],
                                    TYPE_UNIMPLEMENTED_DEVICE);
        } else {
            g_assert_not_reached();
        }
    }

    object_initialize_child(obj, "secctl", &s->secctl, TYPE_IOTKIT_SECCTL);

    for (i = 0; i < ARRAY_SIZE(s->apb_ppc); i++) {
        g_autofree char *name = g_strdup_printf("apb-ppc%d", i);
        object_initialize_child(obj, name, &s->apb_ppc[i], TYPE_TZ_PPC);
    }

    for (i = 0; i < info->sram_banks; i++) {
        char *name = g_strdup_printf("mpc%d", i);
        object_initialize_child(obj, name, &s->mpc[i], TYPE_TZ_MPC);
        g_free(name);
    }
    object_initialize_child(obj, "mpc-irq-orgate", &s->mpc_irq_orgate,
                            TYPE_OR_IRQ);

    for (i = 0; i < IOTS_NUM_EXP_MPC + info->sram_banks; i++) {
        char *name = g_strdup_printf("mpc-irq-splitter-%d", i);
        SplitIRQ *splitter = &s->mpc_irq_splitter[i];

        object_initialize_child(obj, name, splitter, TYPE_SPLIT_IRQ);
        g_free(name);
    }

    if (info->has_mhus) {
        object_initialize_child(obj, "mhu0", &s->mhu[0], TYPE_ARMSSE_MHU);
        object_initialize_child(obj, "mhu1", &s->mhu[1], TYPE_ARMSSE_MHU);
    }
    if (info->has_cachectrl) {
        for (i = 0; i < info->num_cpus; i++) {
            char *name = g_strdup_printf("cachectrl%d", i);

            object_initialize_child(obj, name, &s->cachectrl[i],
                                    TYPE_UNIMPLEMENTED_DEVICE);
            g_free(name);
        }
    }
    if (info->has_cpusecctrl) {
        for (i = 0; i < info->num_cpus; i++) {
            char *name = g_strdup_printf("cpusecctrl%d", i);

            object_initialize_child(obj, name, &s->cpusecctrl[i],
                                    TYPE_UNIMPLEMENTED_DEVICE);
            g_free(name);
        }
    }
    if (info->has_cpuid) {
        for (i = 0; i < info->num_cpus; i++) {
            char *name = g_strdup_printf("cpuid%d", i);

            object_initialize_child(obj, name, &s->cpuid[i],
                                    TYPE_ARMSSE_CPUID);
            g_free(name);
        }
    }
    if (info->has_cpu_pwrctrl) {
        for (i = 0; i < info->num_cpus; i++) {
            char *name = g_strdup_printf("cpu_pwrctrl%d", i);

            object_initialize_child(obj, name, &s->cpu_pwrctrl[i],
                                    TYPE_ARMSSE_CPU_PWRCTRL);
            g_free(name);
        }
    }
    if (info->has_sse_counter) {
        object_initialize_child(obj, "sse-counter", &s->sse_counter,
                                TYPE_SSE_COUNTER);
    }

    object_initialize_child(obj, "nmi-orgate", &s->nmi_orgate, TYPE_OR_IRQ);
    object_initialize_child(obj, "ppc-irq-orgate", &s->ppc_irq_orgate,
                            TYPE_OR_IRQ);
    object_initialize_child(obj, "sec-resp-splitter", &s->sec_resp_splitter,
                            TYPE_SPLIT_IRQ);
    for (i = 0; i < ARRAY_SIZE(s->ppc_irq_splitter); i++) {
        char *name = g_strdup_printf("ppc-irq-splitter-%d", i);
        SplitIRQ *splitter = &s->ppc_irq_splitter[i];

        object_initialize_child(obj, name, splitter, TYPE_SPLIT_IRQ);
        g_free(name);
    }
    if (info->num_cpus > 1) {
        for (i = 0; i < ARRAY_SIZE(s->cpu_irq_splitter); i++) {
            if (info->irq_is_common[i]) {
                char *name = g_strdup_printf("cpu-irq-splitter%d", i);
                SplitIRQ *splitter = &s->cpu_irq_splitter[i];

                object_initialize_child(obj, name, splitter, TYPE_SPLIT_IRQ);
                g_free(name);
            }
        }
    }
}

static void armsse_exp_irq(void *opaque, int n, int level)
{
    qemu_irq *irqarray = opaque;

    qemu_set_irq(irqarray[n], level);
}

static void armsse_mpcexp_status(void *opaque, int n, int level)
{
    ARMSSE *s = ARM_SSE(opaque);
    qemu_set_irq(s->mpcexp_status_in[n], level);
}

static qemu_irq armsse_get_common_irq_in(ARMSSE *s, int irqno)
{
    ARMSSEClass *asc = ARM_SSE_GET_CLASS(s);
    const ARMSSEInfo *info = asc->info;

    assert(info->irq_is_common[irqno]);

    if (info->num_cpus == 1) {
        return qdev_get_gpio_in(DEVICE(&s->armv7m[0]), irqno);
    } else {
        return qdev_get_gpio_in(DEVICE(&s->cpu_irq_splitter[irqno]), 0);
    }
}

static void armsse_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    ARMSSE *s = ARM_SSE(dev);
    ARMSSEClass *asc = ARM_SSE_GET_CLASS(dev);
    const ARMSSEInfo *info = asc->info;
    const ARMSSEDeviceInfo *devinfo;
    int i;
    MemoryRegion *mr;
    SysBusDevice *sbd_apb_ppc0;
    SysBusDevice *sbd_secctl;
    DeviceState *dev_apb_ppc0;
    DeviceState *dev_apb_ppc1;
    DeviceState *dev_secctl;
    DeviceState *dev_splitter;
    uint32_t addr_width_max;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    if (!clock_has_source(s->mainclk)) {
        error_setg(errp, "MAINCLK clock was not connected");
    }
    if (!clock_has_source(s->s32kclk)) {
        error_setg(errp, "S32KCLK clock was not connected");
    }

    assert(info->num_cpus <= SSE_MAX_CPUS);

    assert(is_power_of_2(info->sram_banks));
    addr_width_max = 24 - ctz32(info->sram_banks);
    if (s->sram_addr_width < 1 || s->sram_addr_width > addr_width_max) {
        error_setg(errp, "SRAM_ADDR_WIDTH must be between 1 and %d",
                   addr_width_max);
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -2);

    for (i = 0; i < info->num_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->armv7m[i]);
        Object *cpuobj = OBJECT(&s->armv7m[i]);
        int j;
        char *gpioname;

        qdev_connect_clock_in(cpudev, "cpuclk", s->mainclk);

        qdev_prop_set_uint32(cpudev, "num-irq", s->exp_numirq + NUM_SSE_IRQS);
        qdev_prop_set_uint32(cpudev, "init-svtor", s->init_svtor);
        if (extract32(info->cpuwait_rst, i, 1)) {
            object_property_set_bool(cpuobj, "start-powered-off", true,
                                     &error_abort);
        }
        if (!s->cpu_fpu[i]) {
            if (!object_property_set_bool(cpuobj, "vfp", false, errp)) {
                return;
            }
        }
        if (!s->cpu_dsp[i]) {
            if (!object_property_set_bool(cpuobj, "dsp", false, errp)) {
                return;
            }
        }
        if (!object_property_set_uint(cpuobj, "mpu-ns-regions",
                                      s->cpu_mpu_ns[i], errp)) {
            return;
        }
        if (!object_property_set_uint(cpuobj, "mpu-s-regions",
                                      s->cpu_mpu_s[i], errp)) {
            return;
        }

        if (i > 0) {
            memory_region_add_subregion_overlap(&s->cpu_container[i], 0,
                                                &s->container_alias[i - 1], -1);
        } else {
            memory_region_add_subregion_overlap(&s->cpu_container[i], 0,
                                                &s->container, -1);
        }
        object_property_set_link(cpuobj, "memory",
                                 OBJECT(&s->cpu_container[i]), &error_abort);
        object_property_set_link(cpuobj, "idau", OBJECT(s), &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(cpuobj), errp)) {
            return;
        }
        if (!qdev_realize(DEVICE(&s->cluster[i]), NULL, errp)) {
            return;
        }

        s->exp_irqs[i] = g_new(qemu_irq, s->exp_numirq);
        for (j = 0; j < s->exp_numirq; j++) {
            s->exp_irqs[i][j] = qdev_get_gpio_in(cpudev, j + NUM_SSE_IRQS);
        }
        if (i == 0) {
            gpioname = g_strdup("EXP_IRQ");
        } else {
            gpioname = g_strdup_printf("EXP_CPU%d_IRQ", i);
        }
        qdev_init_gpio_in_named_with_opaque(dev, armsse_exp_irq,
                                            s->exp_irqs[i],
                                            gpioname, s->exp_numirq);
        g_free(gpioname);
    }

    if (info->num_cpus > 1) {
        for (i = 0; i < ARRAY_SIZE(s->cpu_irq_splitter); i++) {
            if (info->irq_is_common[i]) {
                Object *splitter = OBJECT(&s->cpu_irq_splitter[i]);
                DeviceState *devs = DEVICE(splitter);
                int cpunum;

                if (!object_property_set_int(splitter, "num-lines",
                                             info->num_cpus, errp)) {
                    return;
                }
                if (!qdev_realize(DEVICE(splitter), NULL, errp)) {
                    return;
                }
                for (cpunum = 0; cpunum < info->num_cpus; cpunum++) {
                    DeviceState *cpudev = DEVICE(&s->armv7m[cpunum]);

                    qdev_connect_gpio_out(devs, cpunum,
                                          qdev_get_gpio_in(cpudev, i));
                }
            }
        }
    }

    make_alias(s, &s->alias1, &s->container, "alias 1",
               0x10000000, 0x10000000, 0x00000000);
    make_alias(s, &s->alias2, &s->container,
               "alias 2", 0x30000000, 0x10000000, 0x20000000);
    for (i = 0; i < info->num_cpus; i++) {
        make_alias(s, &s->alias3[i], &s->cpu_container[i],
                   "alias 3", 0x50000000, 0x10000000, 0x40000000);
    }

    object_property_set_int(OBJECT(&s->secctl), "sse-version",
                            info->sse_version, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->secctl), errp)) {
        return;
    }
    sbd_secctl = SYS_BUS_DEVICE(&s->secctl);
    dev_secctl = DEVICE(&s->secctl);
    sysbus_mmio_map(sbd_secctl, 0, 0x50080000);
    sysbus_mmio_map(sbd_secctl, 1, 0x40080000);

    s->nsc_cfg_in = qemu_allocate_irq(nsccfg_handler, s, 1);
    qdev_connect_gpio_out_named(dev_secctl, "nsc_cfg", 0, s->nsc_cfg_in);

    if (!object_property_set_int(OBJECT(&s->sec_resp_splitter),
                                 "num-lines", 3, errp)) {
        return;
    }
    if (!qdev_realize(DEVICE(&s->sec_resp_splitter), NULL, errp)) {
        return;
    }
    dev_splitter = DEVICE(&s->sec_resp_splitter);
    qdev_connect_gpio_out_named(dev_secctl, "sec_resp_cfg", 0,
                                qdev_get_gpio_in(dev_splitter, 0));

    for (i = 0; i < info->sram_banks; i++) {
        char *ramname = g_strdup_printf("armsse.sram%d", i);
        SysBusDevice *sbd_mpc;
        uint32_t sram_bank_size = 1 << s->sram_addr_width;

        memory_region_init_ram(&s->sram[i], NULL, ramname,
                               sram_bank_size, errp);
        g_free(ramname);
        if (*errp) {
            return;
        }
        object_property_set_link(OBJECT(&s->mpc[i]), "downstream",
                                 OBJECT(&s->sram[i]), &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->mpc[i]), errp)) {
            return;
        }
        sbd_mpc = SYS_BUS_DEVICE(&s->mpc[i]);
        memory_region_add_subregion(&s->container,
                                    info->sram_bank_base + i * sram_bank_size,
                                    sysbus_mmio_get_region(sbd_mpc, 1));
        memory_region_add_subregion(&s->container, 0x50083000 + i * 0x1000,
                                    sysbus_mmio_get_region(sbd_mpc, 0));
    }

    if (!object_property_set_int(OBJECT(&s->mpc_irq_orgate), "num-lines",
                                 IOTS_NUM_EXP_MPC + info->sram_banks,
                                 errp)) {
        return;
    }
    if (!qdev_realize(DEVICE(&s->mpc_irq_orgate), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->mpc_irq_orgate), 0,
                          armsse_get_common_irq_in(s, 9));

    if (!object_property_set_int(OBJECT(&s->nmi_orgate), "num-lines", 2,
                                 errp)) {
        return;
    }
    if (!qdev_realize(DEVICE(&s->nmi_orgate), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->nmi_orgate), 0,
                          qdev_get_gpio_in_named(DEVICE(&s->armv7m), "NMI", 0));

    if (info->has_sse_counter) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->sse_counter);

        qdev_connect_clock_in(DEVICE(sbd), "CLK", s->mainclk);
        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion(&s->container, 0x58100000,
                                    sysbus_mmio_get_region(sbd, 0));
        memory_region_add_subregion(&s->container, 0x48101000,
                                    sysbus_mmio_get_region(sbd, 1));
    }

    if (info->has_tcms) {
        memory_region_init_ram(&s->itcm, NULL, "sse300-itcm", 512 * KiB, errp);
        if (*errp) {
            return;
        }
        memory_region_init_ram(&s->dtcm, NULL, "sse300-dtcm", 512 * KiB, errp);
        if (*errp) {
            return;
        }
        memory_region_add_subregion(&s->container, 0x00000000, &s->itcm);
        memory_region_add_subregion(&s->container, 0x20000000, &s->dtcm);
    }

    for (devinfo = info->devinfo; devinfo->name; devinfo++) {
        SysBusDevice *sbd;
        qemu_irq irq;

        if (!strcmp(devinfo->type, TYPE_CMSDK_APB_TIMER)) {
            sbd = SYS_BUS_DEVICE(&s->timer[devinfo->index]);

            qdev_connect_clock_in(DEVICE(sbd), "pclk",
                                  devinfo->slowclk ? s->s32kclk : s->mainclk);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else if (!strcmp(devinfo->type, TYPE_CMSDK_APB_DUALTIMER)) {
            sbd = SYS_BUS_DEVICE(&s->dualtimer);

            qdev_connect_clock_in(DEVICE(sbd), "TIMCLK", s->mainclk);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else if (!strcmp(devinfo->type, TYPE_SSE_TIMER)) {
            sbd = SYS_BUS_DEVICE(&s->sse_timer[devinfo->index]);

            assert(info->has_sse_counter);
            object_property_set_link(OBJECT(sbd), "counter",
                                     OBJECT(&s->sse_counter), &error_abort);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else if (!strcmp(devinfo->type, TYPE_CMSDK_APB_WATCHDOG)) {
            sbd = SYS_BUS_DEVICE(&s->cmsdk_watchdog[devinfo->index]);

            qdev_connect_clock_in(DEVICE(sbd), "WDOGCLK",
                                  devinfo->slowclk ? s->s32kclk : s->mainclk);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else if (!strcmp(devinfo->type, TYPE_IOTKIT_SYSINFO)) {
            sbd = SYS_BUS_DEVICE(&s->sysinfo);

            object_property_set_int(OBJECT(&s->sysinfo), "SYS_VERSION",
                                    info->sys_version, &error_abort);
            object_property_set_int(OBJECT(&s->sysinfo), "SYS_CONFIG",
                                    armsse_sys_config_value(s, info),
                                    &error_abort);
            object_property_set_int(OBJECT(&s->sysinfo), "sse-version",
                                    info->sse_version, &error_abort);
            object_property_set_int(OBJECT(&s->sysinfo), "IIDR",
                                    info->iidr, &error_abort);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else if (!strcmp(devinfo->type, TYPE_IOTKIT_SYSCTL)) {
            sbd = SYS_BUS_DEVICE(&s->sysctl);

            object_property_set_int(OBJECT(&s->sysctl), "sse-version",
                                    info->sse_version, &error_abort);
            object_property_set_int(OBJECT(&s->sysctl), "CPUWAIT_RST",
                                    info->cpuwait_rst, &error_abort);
            object_property_set_int(OBJECT(&s->sysctl), "INITSVTOR0_RST",
                                    s->init_svtor, &error_abort);
            object_property_set_int(OBJECT(&s->sysctl), "INITSVTOR1_RST",
                                    s->init_svtor, &error_abort);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else if (!strcmp(devinfo->type, TYPE_UNIMPLEMENTED_DEVICE)) {
            sbd = SYS_BUS_DEVICE(&s->unimp[devinfo->index]);

            qdev_prop_set_string(DEVICE(sbd), "name", devinfo->name);
            qdev_prop_set_uint64(DEVICE(sbd), "size", devinfo->size);
            if (!sysbus_realize(sbd, errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(sbd, 0);
        } else {
            g_assert_not_reached();
        }

        switch (devinfo->irq) {
        case NO_IRQ:
            irq = NULL;
            break;
        case 0 ... NUM_SSE_IRQS - 1:
            irq = armsse_get_common_irq_in(s, devinfo->irq);
            break;
        case NMI_0:
        case NMI_1:
            irq = qdev_get_gpio_in(DEVICE(&s->nmi_orgate),
                                   devinfo->irq - NMI_0);
            break;
        default:
            g_assert_not_reached();
        }

        if (irq) {
            sysbus_connect_irq(sbd, 0, irq);
        }

        if (devinfo->ppc != NO_PPC) {
            TZPPC *ppc = &s->apb_ppc[devinfo->ppc];
            g_autofree char *portname = g_strdup_printf("port[%d]",
                                                        devinfo->ppc_port);
            object_property_set_link(OBJECT(ppc), portname, OBJECT(mr),
                                     &error_abort);
        } else {
            memory_region_add_subregion(&s->container, devinfo->addr, mr);
        }
    }

    if (info->has_mhus) {
        assert(info->num_cpus == ARRAY_SIZE(s->mhu));

        for (i = 0; i < ARRAY_SIZE(s->mhu); i++) {
            char *port;
            int cpunum;
            SysBusDevice *mhu_sbd = SYS_BUS_DEVICE(&s->mhu[i]);

            if (!sysbus_realize(SYS_BUS_DEVICE(&s->mhu[i]), errp)) {
                return;
            }
            port = g_strdup_printf("port[%d]", i + 3);
            mr = sysbus_mmio_get_region(mhu_sbd, 0);
            object_property_set_link(OBJECT(&s->apb_ppc[0]), port, OBJECT(mr),
                                     &error_abort);
            g_free(port);

            for (cpunum = 0; cpunum < info->num_cpus; cpunum++) {
                DeviceState *cpudev = DEVICE(&s->armv7m[cpunum]);

                sysbus_connect_irq(mhu_sbd, cpunum,
                                   qdev_get_gpio_in(cpudev, 6 + i));
            }
        }
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->apb_ppc[0]), errp)) {
        return;
    }

    sbd_apb_ppc0 = SYS_BUS_DEVICE(&s->apb_ppc[0]);
    dev_apb_ppc0 = DEVICE(&s->apb_ppc[0]);

    if (info->has_mhus) {
        mr = sysbus_mmio_get_region(sbd_apb_ppc0, 3);
        memory_region_add_subregion(&s->container, 0x40003000, mr);
        mr = sysbus_mmio_get_region(sbd_apb_ppc0, 4);
        memory_region_add_subregion(&s->container, 0x40004000, mr);
    }
    for (i = 0; i < IOTS_APB_PPC0_NUM_PORTS; i++) {
        qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_nonsec", i,
                                    qdev_get_gpio_in_named(dev_apb_ppc0,
                                                           "cfg_nonsec", i));
        qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_ap", i,
                                    qdev_get_gpio_in_named(dev_apb_ppc0,
                                                           "cfg_ap", i));
    }
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_irq_enable", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc0,
                                                       "irq_enable", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc0_irq_clear", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc0,
                                                       "irq_clear", 0));
    qdev_connect_gpio_out(dev_splitter, 0,
                          qdev_get_gpio_in_named(dev_apb_ppc0,
                                                 "cfg_sec_resp", 0));

    if (!object_property_set_int(OBJECT(&s->ppc_irq_orgate),
                                 "num-lines", NUM_PPCS, errp)) {
        return;
    }
    if (!qdev_realize(DEVICE(&s->ppc_irq_orgate), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->ppc_irq_orgate), 0,
                          armsse_get_common_irq_in(s, 10));

    if (info->has_cachectrl) {
        for (i = 0; i < info->num_cpus; i++) {
            char *name = g_strdup_printf("cachectrl%d", i);

            qdev_prop_set_string(DEVICE(&s->cachectrl[i]), "name", name);
            g_free(name);
            qdev_prop_set_uint64(DEVICE(&s->cachectrl[i]), "size", 0x1000);
            if (!sysbus_realize(SYS_BUS_DEVICE(&s->cachectrl[i]), errp)) {
                return;
            }

            mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cachectrl[i]), 0);
            memory_region_add_subregion(&s->cpu_container[i], 0x50010000, mr);
        }
    }
    if (info->has_cpusecctrl) {
        for (i = 0; i < info->num_cpus; i++) {
            char *name = g_strdup_printf("CPUSECCTRL%d", i);

            qdev_prop_set_string(DEVICE(&s->cpusecctrl[i]), "name", name);
            g_free(name);
            qdev_prop_set_uint64(DEVICE(&s->cpusecctrl[i]), "size", 0x1000);
            if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpusecctrl[i]), errp)) {
                return;
            }

            mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cpusecctrl[i]), 0);
            memory_region_add_subregion(&s->cpu_container[i], 0x50011000, mr);
        }
    }
    if (info->has_cpuid) {
        for (i = 0; i < info->num_cpus; i++) {

            qdev_prop_set_uint32(DEVICE(&s->cpuid[i]), "CPUID", i);
            if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpuid[i]), errp)) {
                return;
            }

            mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cpuid[i]), 0);
            memory_region_add_subregion(&s->cpu_container[i], 0x4001F000, mr);
        }
    }
    if (info->has_cpu_pwrctrl) {
        for (i = 0; i < info->num_cpus; i++) {

            if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpu_pwrctrl[i]), errp)) {
                return;
            }

            mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cpu_pwrctrl[i]), 0);
            memory_region_add_subregion(&s->cpu_container[i], 0x40012000, mr);
        }
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->apb_ppc[1]), errp)) {
        return;
    }

    dev_apb_ppc1 = DEVICE(&s->apb_ppc[1]);
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_nonsec", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "cfg_nonsec", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_ap", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "cfg_ap", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_irq_enable", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "irq_enable", 0));
    qdev_connect_gpio_out_named(dev_secctl, "apb_ppc1_irq_clear", 0,
                                qdev_get_gpio_in_named(dev_apb_ppc1,
                                                       "irq_clear", 0));
    qdev_connect_gpio_out(dev_splitter, 1,
                          qdev_get_gpio_in_named(dev_apb_ppc1,
                                                 "cfg_sec_resp", 0));

    for (devinfo = info->devinfo; devinfo->name; devinfo++) {
        SysBusDevice *ppc_sbd;

        if (devinfo->ppc == NO_PPC) {
            continue;
        }
        ppc_sbd = SYS_BUS_DEVICE(&s->apb_ppc[devinfo->ppc]);
        mr = sysbus_mmio_get_region(ppc_sbd, devinfo->ppc_port);
        memory_region_add_subregion(&s->container, devinfo->addr, mr);
    }

    for (i = 0; i < ARRAY_SIZE(s->ppc_irq_splitter); i++) {
        Object *splitter = OBJECT(&s->ppc_irq_splitter[i]);

        if (!object_property_set_int(splitter, "num-lines", 2, errp)) {
            return;
        }
        if (!qdev_realize(DEVICE(splitter), NULL, errp)) {
            return;
        }
    }

    for (i = 0; i < IOTS_NUM_AHB_EXP_PPC; i++) {
        char *ppcname = g_strdup_printf("ahb_ppcexp%d", i);

        armsse_forward_ppc(s, ppcname, i);
        g_free(ppcname);
    }

    for (i = 0; i < IOTS_NUM_APB_EXP_PPC; i++) {
        char *ppcname = g_strdup_printf("apb_ppcexp%d", i);

        armsse_forward_ppc(s, ppcname, i + IOTS_NUM_AHB_EXP_PPC);
        g_free(ppcname);
    }

    for (i = NUM_EXTERNAL_PPCS; i < NUM_PPCS; i++) {
        DeviceState *devs = DEVICE(&s->ppc_irq_splitter[i]);
        char *gpioname = g_strdup_printf("apb_ppc%d_irq_status",
                                         i - NUM_EXTERNAL_PPCS);
        TZPPC *ppc = &s->apb_ppc[i - NUM_EXTERNAL_PPCS];

        qdev_connect_gpio_out(devs, 0,
                              qdev_get_gpio_in_named(dev_secctl, gpioname, 0));
        qdev_connect_gpio_out(devs, 1,
                              qdev_get_gpio_in(DEVICE(&s->ppc_irq_orgate), i));
        qdev_connect_gpio_out_named(DEVICE(ppc), "irq", 0,
                                    qdev_get_gpio_in(devs, 0));
        g_free(gpioname);
    }

    for (i = 0; i < IOTS_NUM_EXP_MPC + info->sram_banks; i++) {
        SplitIRQ *splitter = &s->mpc_irq_splitter[i];
        DeviceState *devs = DEVICE(splitter);

        if (!object_property_set_int(OBJECT(splitter), "num-lines", 2,
                                     errp)) {
            return;
        }
        if (!qdev_realize(DEVICE(splitter), NULL, errp)) {
            return;
        }

        if (i < IOTS_NUM_EXP_MPC) {
            s->mpcexp_status_in[i] = qdev_get_gpio_in(devs, 0);
            qdev_connect_gpio_out(devs, 0,
                                  qdev_get_gpio_in_named(dev_secctl,
                                                         "mpcexp_status", i));
        } else {
            qdev_connect_gpio_out_named(DEVICE(&s->mpc[i - IOTS_NUM_EXP_MPC]),
                                        "irq", 0,
                                        qdev_get_gpio_in(devs, 0));
            qdev_connect_gpio_out(devs, 0,
                                  qdev_get_gpio_in_named(dev_secctl,
                                                         "mpc_status",
                                                         i - IOTS_NUM_EXP_MPC));
        }

        qdev_connect_gpio_out(devs, 1,
                              qdev_get_gpio_in(DEVICE(&s->mpc_irq_orgate), i));
    }
    qdev_init_gpio_in_named(dev, armsse_mpcexp_status, "mpcexp_status",
                            IOTS_NUM_EXP_MPC);

    armsse_forward_sec_resp_cfg(s);

    qdev_pass_gpios(dev_secctl, dev, "mscexp_status");
    qdev_pass_gpios(dev_secctl, dev, "mscexp_clear");
    qdev_pass_gpios(dev_secctl, dev, "mscexp_ns");
    qdev_connect_gpio_out_named(dev_secctl, "msc_irq", 0,
                                armsse_get_common_irq_in(s, 11));

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->container);
}

static void armsse_idau_check(IDAUInterface *ii, uint32_t address,
                              int *iregion, bool *exempt, bool *ns, bool *nsc)
{
    ARMSSE *s = ARM_SSE(ii);
    int region = extract32(address, 28, 4);

    *ns = !(region & 1);
    *nsc = (region == 1 && (s->nsccfg & 1)) || (region == 3 && (s->nsccfg & 2));
    *exempt = (address & 0xeff00000) == 0xe0000000;
    *iregion = region;
}

static const VMStateDescription armsse_vmstate = {
    .name = "iotkit",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(mainclk, ARMSSE),
        VMSTATE_CLOCK(s32kclk, ARMSSE),
        VMSTATE_UINT32(nsccfg, ARMSSE),
        VMSTATE_END_OF_LIST()
    }
};

static void armsse_reset(DeviceState *dev)
{
    ARMSSE *s = ARM_SSE(dev);

    s->nsccfg = 0;
}

static void armsse_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDAUInterfaceClass *iic = IDAU_INTERFACE_CLASS(klass);
    ARMSSEClass *asc = ARM_SSE_CLASS(klass);
    const ARMSSEInfo *info = data;

    dc->realize = armsse_realize;
    dc->vmsd = &armsse_vmstate;
    device_class_set_props_n(dc, info->props, info->props_count);
    device_class_set_legacy_reset(dc, armsse_reset);
    iic->check = armsse_idau_check;
    asc->info = info;
}

static const TypeInfo armsse_info = {
    .name = TYPE_ARM_SSE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMSSE),
    .class_size = sizeof(ARMSSEClass),
    .instance_init = armsse_init,
    .abstract = true,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_IDAU_INTERFACE },
        { }
    }
};

static void armsse_register_types(void)
{
    int i;

    type_register_static(&armsse_info);

    for (i = 0; i < ARRAY_SIZE(armsse_variants); i++) {
        TypeInfo ti = {
            .name = armsse_variants[i].name,
            .parent = TYPE_ARM_SSE,
            .class_init = armsse_class_init,
            .class_data = &armsse_variants[i],
        };
        type_register_static(&ti);
    }
}

type_init(armsse_register_types);
