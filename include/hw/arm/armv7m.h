/*
 * ARMv7M CPU object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_H
#define HW_ARM_ARMV7M_H

#include "hw/sysbus.h"
#include "hw/intc/armv7m_nvic.h"
#include "hw/misc/armv7m_ras.h"
#include "target/arm/idau.h"
#include "qom/object.h"
#include "hw/clock.h"

#define TYPE_BITBAND "ARM-bitband-memory"
OBJECT_DECLARE_SIMPLE_TYPE(BitBandState, BITBAND)

struct BitBandState {
    SysBusDevice parent_obj;

    AddressSpace source_as;
    MemoryRegion iomem;
    uint32_t base;
    MemoryRegion *source_memory;
};

#define TYPE_ARMV7M "armv7m"
OBJECT_DECLARE_SIMPLE_TYPE(ARMv7MState, ARMV7M)

#define ARMV7M_NUM_BITBANDS 2

struct ARMv7MState {
    SysBusDevice parent_obj;
    NVICState nvic;
    BitBandState bitband[ARMV7M_NUM_BITBANDS];
    ARMCPU *cpu;
    ARMv7MRAS ras;
    SysTickState systick[M_REG_NUM_BANKS];

    MemoryRegion container;
    MemoryRegion systickmem;
    MemoryRegion systick_ns_mem;
    MemoryRegion sysreg_ns_mem;
    MemoryRegion defaultmem;

    Clock *refclk;
    Clock *cpuclk;

    char *cpu_type;
    MemoryRegion *board_memory;
    Object *idau;
    uint32_t init_svtor;
    uint32_t init_nsvtor;
    uint32_t mpu_ns_regions;
    uint32_t mpu_s_regions;
    bool enable_bitband;
    bool start_powered_off;
    bool vfp;
    bool dsp;
};

#endif
