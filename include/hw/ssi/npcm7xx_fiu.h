/*
 * Nuvoton NPCM7xx Flash Interface Unit (FIU)
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
#ifndef NPCM7XX_FIU_H
#define NPCM7XX_FIU_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"

#define NPCM7XX_FIU_NR_REGS (0x7c / sizeof(uint32_t))

typedef struct NPCM7xxFIUState NPCM7xxFIUState;

typedef struct NPCM7xxFIUFlash {
    MemoryRegion direct_access;
    NPCM7xxFIUState *fiu;
} NPCM7xxFIUFlash;

struct NPCM7xxFIUState {
    SysBusDevice parent;

    MemoryRegion mmio;

    int32_t cs_count;
    int32_t active_cs;
    qemu_irq *cs_lines;
    uint64_t flash_size;
    NPCM7xxFIUFlash *flash;

    SSIBus *spi;

    uint32_t regs[NPCM7XX_FIU_NR_REGS];
};

#define TYPE_NPCM7XX_FIU "npcm7xx-fiu"
#define NPCM7XX_FIU(obj) OBJECT_CHECK(NPCM7xxFIUState, (obj), TYPE_NPCM7XX_FIU)

#endif
