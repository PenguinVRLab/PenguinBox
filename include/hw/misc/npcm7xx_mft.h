/*
 * Nuvoton NPCM7xx MFT Module
 *
 * Copyright 2021 Google LLC
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
#ifndef NPCM7XX_MFT_H
#define NPCM7XX_MFT_H

#include "system/memory.h"
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define NPCM7XX_MFT_MAX_FAN_INPUT 19

#define NPCM7XX_MFT_NR_REGS (0x20 / sizeof(uint16_t))

#define NPCM7XX_MFT_FANIN_COUNT 4

struct NPCM7xxMFTState {
    SysBusDevice parent;

    MemoryRegion iomem;

    Clock       *clock_in;
    Clock       *clock_1, *clock_2;
    qemu_irq    irq;
    uint16_t    regs[NPCM7XX_MFT_NR_REGS];

    uint32_t    max_rpm[NPCM7XX_MFT_FANIN_COUNT];
    uint32_t    duty[NPCM7XX_MFT_FANIN_COUNT];
};

#define TYPE_NPCM7XX_MFT "npcm7xx-mft"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxMFTState, NPCM7XX_MFT)

#endif
