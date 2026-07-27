/*
 * Freescale i.MX25 SoC emulation
 *
 * Copyright (C) 2015 Jean-Christophe Dubois <jcd@tribudubois.net>
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

#ifndef FSL_IMX25_H
#define FSL_IMX25_H

#include "hw/intc/imx_avic.h"
#include "hw/misc/imx25_ccm.h"
#include "hw/char/imx_serial.h"
#include "hw/timer/imx_gpt.h"
#include "hw/timer/imx_epit.h"
#include "hw/net/imx_fec.h"
#include "hw/misc/imx_rngc.h"
#include "hw/i2c/imx_i2c.h"
#include "hw/gpio/imx_gpio.h"
#include "hw/sd/sdhci.h"
#include "hw/usb/chipidea.h"
#include "hw/watchdog/wdt_imx2.h"
#include "system/memory.h"
#include "target/arm/cpu.h"
#include "qom/object.h"

#define TYPE_FSL_IMX25 "fsl-imx25"
OBJECT_DECLARE_SIMPLE_TYPE(FslIMX25State, FSL_IMX25)

#define FSL_IMX25_NUM_UARTS 5
#define FSL_IMX25_NUM_GPTS 4
#define FSL_IMX25_NUM_EPITS 2
#define FSL_IMX25_NUM_I2CS 3
#define FSL_IMX25_NUM_GPIOS 4
#define FSL_IMX25_NUM_ESDHCS 2
#define FSL_IMX25_NUM_USBS 2

struct FslIMX25State {
    DeviceState parent_obj;

    ARMCPU         cpu;
    IMXAVICState   avic;
    IMX25CCMState  ccm;
    IMXSerialState uart[FSL_IMX25_NUM_UARTS];
    IMXGPTState    gpt[FSL_IMX25_NUM_GPTS];
    IMXEPITState   epit[FSL_IMX25_NUM_EPITS];
    IMXFECState    fec;
    IMXRNGCState   rngc;
    IMXI2CState    i2c[FSL_IMX25_NUM_I2CS];
    IMXGPIOState   gpio[FSL_IMX25_NUM_GPIOS];
    SDHCIState     esdhc[FSL_IMX25_NUM_ESDHCS];
    ChipideaState  usb[FSL_IMX25_NUM_USBS];
    IMX2WdtState   wdt;
    MemoryRegion   rom[2];
    MemoryRegion   iram;
    MemoryRegion   iram_alias;
    uint32_t       phy_num;
};

#define FSL_IMX25_ROM0_ADDR     0x00000000
#define FSL_IMX25_ROM0_SIZE     0x4000
#define FSL_IMX25_ROM1_ADDR     0x00404000
#define FSL_IMX25_ROM1_SIZE     0x4000
#define FSL_IMX25_I2C1_ADDR     0x43F80000
#define FSL_IMX25_I2C1_SIZE     0x4000
#define FSL_IMX25_I2C3_ADDR     0x43F84000
#define FSL_IMX25_I2C3_SIZE     0x4000
#define FSL_IMX25_UART1_ADDR    0x43F90000
#define FSL_IMX25_UART1_SIZE    0x4000
#define FSL_IMX25_UART2_ADDR    0x43F94000
#define FSL_IMX25_UART2_SIZE    0x4000
#define FSL_IMX25_I2C2_ADDR     0x43F98000
#define FSL_IMX25_I2C2_SIZE     0x4000
#define FSL_IMX25_UART4_ADDR    0x50008000
#define FSL_IMX25_UART4_SIZE    0x4000
#define FSL_IMX25_UART3_ADDR    0x5000C000
#define FSL_IMX25_UART3_SIZE    0x4000
#define FSL_IMX25_UART5_ADDR    0x5002C000
#define FSL_IMX25_UART5_SIZE    0x4000
#define FSL_IMX25_FEC_ADDR      0x50038000
#define FSL_IMX25_CCM_ADDR      0x53F80000
#define FSL_IMX25_CCM_SIZE      0x4000
#define FSL_IMX25_GPT4_ADDR     0x53F84000
#define FSL_IMX25_GPT4_SIZE     0x4000
#define FSL_IMX25_GPT3_ADDR     0x53F88000
#define FSL_IMX25_GPT3_SIZE     0x4000
#define FSL_IMX25_GPT2_ADDR     0x53F8C000
#define FSL_IMX25_GPT2_SIZE     0x4000
#define FSL_IMX25_GPT1_ADDR     0x53F90000
#define FSL_IMX25_GPT1_SIZE     0x4000
#define FSL_IMX25_EPIT1_ADDR    0x53F94000
#define FSL_IMX25_EPIT1_SIZE    0x4000
#define FSL_IMX25_EPIT2_ADDR    0x53F98000
#define FSL_IMX25_EPIT2_SIZE    0x4000
#define FSL_IMX25_GPIO4_ADDR    0x53F9C000
#define FSL_IMX25_GPIO4_SIZE    0x4000
#define FSL_IMX25_GPIO3_ADDR    0x53FA4000
#define FSL_IMX25_GPIO3_SIZE    0x4000
#define FSL_IMX25_RNGC_ADDR     0x53FB0000
#define FSL_IMX25_RNGC_SIZE     0x4000
#define FSL_IMX25_ESDHC1_ADDR   0x53FB4000
#define FSL_IMX25_ESDHC1_SIZE   0x4000
#define FSL_IMX25_ESDHC2_ADDR   0x53FB8000
#define FSL_IMX25_ESDHC2_SIZE   0x4000
#define FSL_IMX25_GPIO1_ADDR    0x53FCC000
#define FSL_IMX25_GPIO1_SIZE    0x4000
#define FSL_IMX25_GPIO2_ADDR    0x53FD0000
#define FSL_IMX25_GPIO2_SIZE    0x4000
#define FSL_IMX25_WDT_ADDR      0x53FDC000
#define FSL_IMX25_WDT_SIZE      0x4000
#define FSL_IMX25_USB1_ADDR     0x53FF4000
#define FSL_IMX25_USB1_SIZE     0x0200
#define FSL_IMX25_USB2_ADDR     0x53FF4400
#define FSL_IMX25_USB2_SIZE     0x0200
#define FSL_IMX25_AVIC_ADDR     0x68000000
#define FSL_IMX25_AVIC_SIZE     0x4000
#define FSL_IMX25_IRAM_ADDR     0x78000000
#define FSL_IMX25_IRAM_SIZE     0x20000
#define FSL_IMX25_IRAM_ALIAS_ADDR     0x78020000
#define FSL_IMX25_IRAM_ALIAS_SIZE     0x7FE0000
#define FSL_IMX25_SDRAM0_ADDR   0x80000000
#define FSL_IMX25_SDRAM0_SIZE   0x10000000
#define FSL_IMX25_SDRAM1_ADDR   0x90000000
#define FSL_IMX25_SDRAM1_SIZE   0x10000000

#define FSL_IMX25_UART1_IRQ     45
#define FSL_IMX25_UART2_IRQ     32
#define FSL_IMX25_UART3_IRQ     18
#define FSL_IMX25_UART4_IRQ     5
#define FSL_IMX25_UART5_IRQ     40
#define FSL_IMX25_GPT1_IRQ      54
#define FSL_IMX25_GPT2_IRQ      53
#define FSL_IMX25_GPT3_IRQ      29
#define FSL_IMX25_GPT4_IRQ      1
#define FSL_IMX25_EPIT1_IRQ     28
#define FSL_IMX25_EPIT2_IRQ     27
#define FSL_IMX25_FEC_IRQ       57
#define FSL_IMX25_RNGC_IRQ      22
#define FSL_IMX25_I2C1_IRQ      3
#define FSL_IMX25_I2C2_IRQ      4
#define FSL_IMX25_I2C3_IRQ      10
#define FSL_IMX25_GPIO1_IRQ     52
#define FSL_IMX25_GPIO2_IRQ     51
#define FSL_IMX25_GPIO3_IRQ     16
#define FSL_IMX25_GPIO4_IRQ     23
#define FSL_IMX25_ESDHC1_IRQ    9
#define FSL_IMX25_ESDHC2_IRQ    8
#define FSL_IMX25_USB1_IRQ      37
#define FSL_IMX25_USB2_IRQ      35
#define FSL_IMX25_WDT_IRQ       55

#endif
