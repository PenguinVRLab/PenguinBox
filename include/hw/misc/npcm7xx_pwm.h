/*
 * Nuvoton NPCM7xx PWM Module
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
#ifndef NPCM7XX_PWM_H
#define NPCM7XX_PWM_H

#include "hw/clock.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define NPCM7XX_PWM_PER_MODULE 4

#define NPCM7XX_PWM_NR_REGS (0x54 / sizeof(uint32_t))

#define NPCM7XX_PWM_MAX_DUTY 1000000
#define TYPE_NPCM7XX_PWM "npcm7xx-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxPWMState, NPCM7XX_PWM)

typedef struct NPCM7xxPWM {
    NPCM7xxPWMState         *module;

    qemu_irq                irq;

    bool                    running;
    bool                    inverted;

    uint8_t                 index;
    uint32_t                cnr;
    uint32_t                cmr;
    uint32_t                pdr;
    uint32_t                pwdr;

    uint32_t                freq;
    uint32_t                duty;
} NPCM7xxPWM;

struct NPCM7xxPWMState {
    SysBusDevice parent;

    MemoryRegion iomem;

    Clock       *clock;
    NPCM7xxPWM  pwm[NPCM7XX_PWM_PER_MODULE];
    qemu_irq    duty_gpio_out[NPCM7XX_PWM_PER_MODULE];

    uint32_t    ppr;
    uint32_t    csr;
    uint32_t    pcr;
    uint32_t    pier;
    uint32_t    piir;
};

#endif
