/*
 * Nuvoton NPCM7xx ADC Module
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
#ifndef NPCM7XX_ADC_H
#define NPCM7XX_ADC_H

#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

#define NPCM7XX_ADC_NUM_INPUTS      8
#define NPCM7XX_ADC_NUM_CALIB       2

struct NPCM7xxADCState {
    SysBusDevice parent;

    MemoryRegion iomem;

    QEMUTimer    conv_timer;

    qemu_irq     irq;
    uint32_t     con;
    uint32_t     data;
    Clock       *clock;

    uint32_t     adci[NPCM7XX_ADC_NUM_INPUTS];
    uint32_t     vref;
    uint32_t     iref;

    uint16_t     calibration_r_values[NPCM7XX_ADC_NUM_CALIB];
};

#define TYPE_NPCM7XX_ADC "npcm7xx-adc"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxADCState, NPCM7XX_ADC)

#endif
