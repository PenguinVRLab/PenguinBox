/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef WDT_IMX2_H
#define WDT_IMX2_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "qom/object.h"

#define TYPE_IMX2_WDT "imx2.wdt"
OBJECT_DECLARE_SIMPLE_TYPE(IMX2WdtState, IMX2_WDT)

enum IMX2WdtRegisters {
    IMX2_WDT_WCR  = 0x0000,
    IMX2_WDT_WSR  = 0x0002,
    IMX2_WDT_WRSR = 0x0004,
    IMX2_WDT_WICR = 0x0006,
    IMX2_WDT_WMCR = 0x0008,
};

#define IMX2_WDT_MMIO_SIZE 0x000a

#define IMX2_WDT_WCR_WT         (0xFF << 8)
#define IMX2_WDT_WCR_WDW        BIT(7)
#define IMX2_WDT_WCR_WDA        BIT(5)
#define IMX2_WDT_WCR_SRS        BIT(4)
#define IMX2_WDT_WCR_WDT        BIT(3)
#define IMX2_WDT_WCR_WDE        BIT(2)
#define IMX2_WDT_WCR_WDBG       BIT(1)
#define IMX2_WDT_WCR_WDZST      BIT(0)

#define IMX2_WDT_WCR_LOCK_MASK  (IMX2_WDT_WCR_WDZST | IMX2_WDT_WCR_WDBG \
                                 | IMX2_WDT_WCR_WDW)

#define IMX2_WDT_SEQ1           0x5555
#define IMX2_WDT_SEQ2           0xAAAA

#define IMX2_WDT_WRSR_TOUT      BIT(1)
#define IMX2_WDT_WRSR_SFTW      BIT(0)

#define IMX2_WDT_WICR_WIE       BIT(15)
#define IMX2_WDT_WICR_WTIS      BIT(14)
#define IMX2_WDT_WICR_WICT      0xff
#define IMX2_WDT_WICR_WICT_DEF  0x04

#define IMX2_WDT_WICR_LOCK_MASK (IMX2_WDT_WICR_WIE | IMX2_WDT_WICR_WICT)

#define IMX2_WDT_WMCR_PDE       BIT(0)

struct IMX2WdtState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    struct ptimer_state *timer;
    struct ptimer_state *itimer;

    bool pretimeout_support;
    bool wicr_locked;

    uint16_t wcr;
    uint16_t wsr;
    uint16_t wrsr;
    uint16_t wicr;
    uint16_t wmcr;

    bool wcr_locked;
    bool wcr_wde_locked;
    bool wcr_wdt_locked;
};

#endif
