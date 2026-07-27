/*
 * ARMv7M SysTick timer
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell
 *
 * This code is licensed under the GPL (version 2 or later).
 */

#ifndef HW_TIMER_ARMV7M_SYSTICK_H
#define HW_TIMER_ARMV7M_SYSTICK_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/ptimer.h"
#include "hw/clock.h"

#define TYPE_SYSTICK "armv7m_systick"

OBJECT_DECLARE_SIMPLE_TYPE(SysTickState, SYSTICK)

struct SysTickState {
    SysBusDevice parent_obj;

    uint32_t control;
    uint32_t reload;
    int64_t tick;
    ptimer_state *ptimer;
    MemoryRegion iomem;
    qemu_irq irq;
    Clock *refclk;
    Clock *cpuclk;
};

#endif
