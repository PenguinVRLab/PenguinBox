/*
 * Nuvoton NPCM7xx SMBus Module.
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
#ifndef NPCM7XX_SMBUS_H
#define NPCM7XX_SMBUS_H

#include "system/memory.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/sysbus.h"

#define NPCM7XX_SMBUS_NR_ADDRS 10

#define NPCM7XX_SMBUS_FIFO_SIZE 16

typedef enum NPCM7xxSMBusStatus {
    NPCM7XX_SMBUS_STATUS_IDLE,
    NPCM7XX_SMBUS_STATUS_SENDING,
    NPCM7XX_SMBUS_STATUS_RECEIVING,
    NPCM7XX_SMBUS_STATUS_NEGACK,
    NPCM7XX_SMBUS_STATUS_STOPPING_LAST_RECEIVE,
    NPCM7XX_SMBUS_STATUS_STOPPING_NEGACK,
} NPCM7xxSMBusStatus;

struct NPCM7xxSMBusState {
    SysBusDevice parent;

    MemoryRegion iomem;

    I2CBus      *bus;
    qemu_irq     irq;

    uint8_t      sda;
    uint8_t      st;
    uint8_t      cst;
    uint8_t      cst2;
    uint8_t      cst3;
    uint8_t      ctl1;
    uint8_t      ctl2;
    uint8_t      ctl3;
    uint8_t      ctl4;
    uint8_t      ctl5;
    uint8_t      addr[NPCM7XX_SMBUS_NR_ADDRS];

    uint8_t      scllt;
    uint8_t      sclht;

    uint8_t      fif_ctl;
    uint8_t      fif_cts;
    uint8_t      fair_per;
    uint8_t      txf_ctl;
    uint8_t      t_out;
    uint8_t      txf_sts;
    uint8_t      rxf_sts;
    uint8_t      rxf_ctl;

    uint8_t      rx_fifo[NPCM7XX_SMBUS_FIFO_SIZE];
    uint8_t      rx_cur;

    NPCM7xxSMBusStatus status;
};

#define TYPE_NPCM7XX_SMBUS "npcm7xx-smbus"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxSMBusState, NPCM7XX_SMBUS)

#endif
