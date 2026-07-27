/*
 * Device model for i.MX UART
 *
 * Copyright (c) 2008 OKL
 * Originally Written by Hans Jiang
 * Copyright (c) 2011 NICTA Pty Ltd.
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IMX_SERIAL_H
#define IMX_SERIAL_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "qemu/fifo32.h"

#define TYPE_IMX_SERIAL "imx.serial"
OBJECT_DECLARE_SIMPLE_TYPE(IMXSerialState, IMX_SERIAL)

#define FIFO_SIZE       32

#define URXD_CHARRDY    (1<<15)
#define URXD_ERR        (1<<14)
#define URXD_OVRRUN     (1<<13)
#define URXD_FRMERR     (1<<12)
#define URXD_BRK        (1<<11)

#define USR1_PARTYER    (1<<15)
#define USR1_RTSS       (1<<14)
#define USR1_TRDY       (1<<13)
#define USR1_RTSD       (1<<12)
#define USR1_ESCF       (1<<11)
#define USR1_FRAMERR    (1<<10)
#define USR1_RRDY       (1<<9)
#define USR1_AGTIM      (1<<8)
#define USR1_DTRD       (1<<7)
#define USR1_RXDS       (1<<6)
#define USR1_AIRINT     (1<<5)
#define USR1_AWAKE      (1<<4)

#define USR2_ADET       (1<<15)
#define USR2_TXFE       (1<<14)
#define USR2_DTRF       (1<<13)
#define USR2_IDLE       (1<<12)
#define USR2_ACST       (1<<11)
#define USR2_RIDELT     (1<<10)
#define USR2_RIIN       (1<<9)
#define USR2_IRINT      (1<<8)
#define USR2_WAKE       (1<<7)
#define USR2_DCDDELT    (1<<6)
#define USR2_DCDIN      (1<<5)
#define USR2_RTSF       (1<<4)
#define USR2_TXDC       (1<<3)
#define USR2_BRCD       (1<<2)
#define USR2_ORE        (1<<1)
#define USR2_RDR        (1<<0)

#define UCR1_TRDYEN     (1<<13)
#define UCR1_RRDYEN     (1<<9)
#define UCR1_TXMPTYEN   (1<<6)
#define UCR1_UARTEN     (1<<0)

#define UCR2_ATEN       (1<<3)
#define UCR2_TXEN       (1<<2)
#define UCR2_RXEN       (1<<1)
#define UCR2_SRST       (1<<0)

#define UCR4_DREN       BIT(0)
#define UCR4_OREN       BIT(1)
#define UCR4_TCEN       BIT(3)
#define UCR4_WKEN       BIT(7)

#define UTS1_TXEMPTY    (1<<6)
#define UTS1_RXEMPTY    (1<<5)
#define UTS1_TXFULL     (1<<4)
#define UTS1_RXFULL     (1<<3)

#define TL_MASK         0x3f

#define BIT_TIME_NS     8681

#define NUM_BITS        8

#define AGE_DURATION_NS (8 * NUM_BITS * BIT_TIME_NS)

struct IMXSerialState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer ageing_timer;
    Fifo32 rx_fifo;

    uint32_t usr1;
    uint32_t usr2;
    uint32_t ucr1;
    uint32_t ucr2;
    uint32_t uts1;
    uint32_t ufcr;

    uint32_t onems;
    uint32_t ubmr;
    uint32_t ubrc;
    uint32_t ucr3;
    uint32_t ucr4;

    qemu_irq irq;
    CharFrontend chr;
};

#endif
