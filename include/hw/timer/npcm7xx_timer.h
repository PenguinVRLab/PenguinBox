/*
 * Nuvoton NPCM7xx Timer Controller
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */
#ifndef NPCM7XX_TIMER_H
#define NPCM7XX_TIMER_H

#include "system/memory.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

#define NPCM7XX_TIMERS_PER_CTRL (5)

#define NPCM7XX_TIMER_NR_REGS (0x54 / sizeof(uint32_t))

#define NPCM7XX_WATCHDOG_BASETIME_SHIFT 14

#define NPCM7XX_WATCHDOG_RESET_GPIO_OUT "npcm7xx-clk-watchdog-reset-gpio-out"

typedef struct NPCM7xxTimerCtrlState NPCM7xxTimerCtrlState;

typedef struct NPCM7xxBaseTimer {
    QEMUTimer   qtimer;
    int64_t     expires_ns;
    int64_t     remaining_ns;
} NPCM7xxBaseTimer;

typedef struct NPCM7xxTimer {
    NPCM7xxTimerCtrlState *ctrl;

    qemu_irq    irq;
    NPCM7xxBaseTimer base_timer;

    uint32_t    tcsr;
    uint32_t    ticr;
} NPCM7xxTimer;

typedef struct NPCM7xxWatchdogTimer {
    NPCM7xxTimerCtrlState *ctrl;

    qemu_irq            irq;
    qemu_irq            reset_signal;
    NPCM7xxBaseTimer base_timer;

    uint32_t            wtcr;
} NPCM7xxWatchdogTimer;

struct NPCM7xxTimerCtrlState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t    tisr;

    Clock       *clock;
    NPCM7xxTimer timer[NPCM7XX_TIMERS_PER_CTRL];
    NPCM7xxWatchdogTimer watchdog_timer;
};

#define TYPE_NPCM7XX_TIMER "npcm7xx-timer"
#define NPCM7XX_TIMER(obj)                                              \
    OBJECT_CHECK(NPCM7xxTimerCtrlState, (obj), TYPE_NPCM7XX_TIMER)

#endif
