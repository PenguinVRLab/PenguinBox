/*
 * auxbus.h
 *
 *  Copyright (C)2014 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HW_MISC_AUXBUS_H
#define HW_MISC_AUXBUS_H

#include "system/memory.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

typedef enum AUXCommand AUXCommand;
typedef enum AUXReply AUXReply;

#define TYPE_AUXTOI2C "aux-to-i2c-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(AUXTOI2CState, AUXTOI2C)

#define TYPE_AUX_SLAVE "aux-slave"
OBJECT_DECLARE_SIMPLE_TYPE(AUXSlave, AUX_SLAVE)

enum AUXCommand {
    WRITE_I2C = 0,
    READ_I2C = 1,
    WRITE_I2C_STATUS = 2,
    WRITE_I2C_MOT = 4,
    READ_I2C_MOT = 5,
    WRITE_AUX = 8,
    READ_AUX = 9
};

enum AUXReply {
    AUX_I2C_ACK = 0,
    AUX_NACK = 1,
    AUX_DEFER = 2,
    AUX_I2C_NACK = 4,
    AUX_I2C_DEFER = 8
};

#define TYPE_AUX_BUS "aux-bus"
OBJECT_DECLARE_SIMPLE_TYPE(AUXBus, AUX_BUS)

struct AUXBus {
    BusState qbus;

    AUXSlave *current_dev;
    AUXSlave *dev;
    uint32_t last_i2c_address;
    AUXCommand last_transaction;

    AUXTOI2CState *bridge;

    MemoryRegion *aux_io;
    AddressSpace aux_addr_space;
};

struct AUXSlave {
    DeviceState parent_obj;

    MemoryRegion *mmio;
};

AUXBus *aux_bus_init(DeviceState *parent, const char *name);

void aux_bus_realize(AUXBus *bus);

AUXReply aux_request(AUXBus *bus, AUXCommand cmd, uint32_t address,
                              uint8_t len, uint8_t *data);

I2CBus *aux_get_i2c_bus(AUXBus *bus);

void aux_init_mmio(AUXSlave *aux_slave, MemoryRegion *mmio);

void aux_map_slave(AUXSlave *dev, hwaddr addr);

#endif
