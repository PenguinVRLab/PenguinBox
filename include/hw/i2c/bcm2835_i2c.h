/*
 * Broadcom Serial Controller (BSC)
 *
 * Copyright (c) 2024 Rayhan Faizel <rayhan.faizel@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_BCM2835_I2C "bcm2835-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835I2CState, BCM2835_I2C)

#define BCM2835_I2C_C       0x0
#define BCM2835_I2C_S       0x4
#define BCM2835_I2C_DLEN    0x8
#define BCM2835_I2C_A       0xc
#define BCM2835_I2C_FIFO    0x10
#define BCM2835_I2C_DIV     0x14
#define BCM2835_I2C_DEL     0x18
#define BCM2835_I2C_CLKT    0x20

#define BCM2835_I2C_C_I2CEN     BIT(15)
#define BCM2835_I2C_C_INTR      BIT(10)
#define BCM2835_I2C_C_INTT      BIT(9)
#define BCM2835_I2C_C_INTD      BIT(8)
#define BCM2835_I2C_C_ST        BIT(7)
#define BCM2835_I2C_C_CLEAR     (BIT(5) | BIT(4))
#define BCM2835_I2C_C_READ      BIT(0)

#define BCM2835_I2C_S_CLKT      BIT(9)
#define BCM2835_I2C_S_ERR       BIT(8)
#define BCM2835_I2C_S_RXF       BIT(7)
#define BCM2835_I2C_S_TXE       BIT(6)
#define BCM2835_I2C_S_RXD       BIT(5)
#define BCM2835_I2C_S_TXD       BIT(4)
#define BCM2835_I2C_S_RXR       BIT(3)
#define BCM2835_I2C_S_TXW       BIT(2)
#define BCM2835_I2C_S_DONE      BIT(1)
#define BCM2835_I2C_S_TA        BIT(0)

struct BCM2835I2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint32_t c;
    uint32_t s;
    uint32_t dlen;
    uint32_t a;
    uint32_t div;
    uint32_t del;
    uint32_t clkt;

    uint32_t last_dlen;
};
