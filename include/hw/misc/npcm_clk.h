/*
 * Nuvoton NPCM7xx/8xx Clock Control Registers.
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
#ifndef NPCM_CLK_H
#define NPCM_CLK_H

#include "system/memory.h"
#include "hw/clock.h"
#include "hw/sysbus.h"

#define NPCM7XX_CLK_NR_REGS             (0x70 / sizeof(uint32_t))
#define NPCM8XX_CLK_NR_REGS             (0xc4 / sizeof(uint32_t))
#define NPCM_CLK_MAX_NR_REGS            NPCM8XX_CLK_NR_REGS

#define NPCM7XX_WATCHDOG_RESET_GPIO_IN "npcm7xx-clk-watchdog-reset-gpio-in"

#define NPCM7XX_CLK_SEL_MAX_INPUT 5

typedef enum NPCM7xxClockPLL {
    NPCM7XX_CLOCK_PLL0,
    NPCM7XX_CLOCK_PLL1,
    NPCM7XX_CLOCK_PLL2,
    NPCM7XX_CLOCK_PLLG,
    NPCM7XX_CLOCK_NR_PLLS,
} NPCM7xxClockPLL;

typedef enum NPCM7xxClockSEL {
    NPCM7XX_CLOCK_PIXCKSEL,
    NPCM7XX_CLOCK_MCCKSEL,
    NPCM7XX_CLOCK_CPUCKSEL,
    NPCM7XX_CLOCK_CLKOUTSEL,
    NPCM7XX_CLOCK_UARTCKSEL,
    NPCM7XX_CLOCK_TIMCKSEL,
    NPCM7XX_CLOCK_SDCKSEL,
    NPCM7XX_CLOCK_GFXMSEL,
    NPCM7XX_CLOCK_SUCKSEL,
    NPCM7XX_CLOCK_NR_SELS,
} NPCM7xxClockSEL;

typedef enum NPCM7xxClockDivider {
    NPCM7XX_CLOCK_PLL1D2,
    NPCM7XX_CLOCK_PLL2D2,
    NPCM7XX_CLOCK_MC_DIVIDER,
    NPCM7XX_CLOCK_AXI_DIVIDER,
    NPCM7XX_CLOCK_AHB_DIVIDER,
    NPCM7XX_CLOCK_AHB3_DIVIDER,
    NPCM7XX_CLOCK_SPI0_DIVIDER,
    NPCM7XX_CLOCK_SPIX_DIVIDER,
    NPCM7XX_CLOCK_APB1_DIVIDER,
    NPCM7XX_CLOCK_APB2_DIVIDER,
    NPCM7XX_CLOCK_APB3_DIVIDER,
    NPCM7XX_CLOCK_APB4_DIVIDER,
    NPCM7XX_CLOCK_APB5_DIVIDER,
    NPCM7XX_CLOCK_CLKOUT_DIVIDER,
    NPCM7XX_CLOCK_UART_DIVIDER,
    NPCM7XX_CLOCK_TIMER_DIVIDER,
    NPCM7XX_CLOCK_ADC_DIVIDER,
    NPCM7XX_CLOCK_MMC_DIVIDER,
    NPCM7XX_CLOCK_SDHC_DIVIDER,
    NPCM7XX_CLOCK_GFXM_DIVIDER,
    NPCM7XX_CLOCK_UTMI_DIVIDER,
    NPCM7XX_CLOCK_NR_DIVIDERS,
} NPCM7xxClockConverter;

typedef struct NPCMCLKState NPCMCLKState;

typedef struct NPCM7xxClockPLLState {
    DeviceState parent;

    const char *name;
    NPCMCLKState *clk;
    Clock *clock_in;
    Clock *clock_out;

    int reg;
} NPCM7xxClockPLLState;

typedef struct NPCM7xxClockSELState {
    DeviceState parent;

    const char *name;
    NPCMCLKState *clk;
    uint8_t input_size;
    Clock *clock_in[NPCM7XX_CLK_SEL_MAX_INPUT];
    Clock *clock_out;

    int offset;
    int len;
} NPCM7xxClockSELState;

typedef struct NPCM7xxClockDividerState {
    DeviceState parent;

    const char *name;
    NPCMCLKState *clk;
    Clock *clock_in;
    Clock *clock_out;

    uint32_t (*divide)(struct NPCM7xxClockDividerState *s);
    union {
        struct {
            int reg;
            int offset;
            int len;
        };
        int divisor;
    };
} NPCM7xxClockDividerState;

struct NPCMCLKState {
    SysBusDevice parent;

    MemoryRegion iomem;

    NPCM7xxClockPLLState plls[NPCM7XX_CLOCK_NR_PLLS];
    NPCM7xxClockSELState sels[NPCM7XX_CLOCK_NR_SELS];
    NPCM7xxClockDividerState dividers[NPCM7XX_CLOCK_NR_DIVIDERS];

    uint32_t regs[NPCM_CLK_MAX_NR_REGS];

    int64_t ref_ns;

    Clock *clkref;
};

typedef struct NPCMCLKClass {
    SysBusDeviceClass parent;

    size_t nr_regs;
    const uint32_t *cold_reset_values;
} NPCMCLKClass;

#define TYPE_NPCM_CLK "npcm-clk"
OBJECT_DECLARE_TYPE(NPCMCLKState, NPCMCLKClass, NPCM_CLK)
#define TYPE_NPCM7XX_CLK "npcm7xx-clk"
#define TYPE_NPCM8XX_CLK "npcm8xx-clk"

#endif
