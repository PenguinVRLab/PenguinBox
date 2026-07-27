/*
 * I2C libqos
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBQOS_I2C_H
#define LIBQOS_I2C_H

#include "../libqtest.h"
#include "qgraph.h"

typedef struct I2CAdapter I2CAdapter;
struct I2CAdapter {
    void (*send)(I2CAdapter *adapter, uint8_t addr,
                 const uint8_t *buf, uint16_t len);
    void (*recv)(I2CAdapter *adapter, uint8_t addr,
                 uint8_t *buf, uint16_t len);

    QTestState *qts;
};

typedef struct QI2CAddress QI2CAddress;
struct QI2CAddress {
    uint8_t addr;
};

typedef struct QI2CDevice QI2CDevice;
struct QI2CDevice {
    QOSGraphObject obj;
    I2CAdapter *bus;
    uint8_t addr;
};

void *i2c_device_create(void *i2c_bus, QGuestAllocator *alloc, void *addr);
void add_qi2c_address(QOSGraphEdgeOptions *opts, QI2CAddress *addr);

void qi2c_send(QI2CDevice *dev, const uint8_t *buf, uint16_t len);
void qi2c_recv(QI2CDevice *dev, uint8_t *buf, uint16_t len);

void i2c_read_block(QI2CDevice *dev, uint8_t reg,
                    uint8_t *buf, uint16_t len);
void i2c_write_block(QI2CDevice *dev, uint8_t reg,
                     const uint8_t *buf, uint16_t len);
uint8_t i2c_get8(QI2CDevice *dev, uint8_t reg);
uint16_t i2c_get16(QI2CDevice *dev, uint8_t reg);
void i2c_set8(QI2CDevice *dev, uint8_t reg, uint8_t value);
void i2c_set16(QI2CDevice *dev, uint8_t reg, uint16_t value);

typedef struct OMAPI2C {
    QOSGraphObject obj;
    I2CAdapter parent;

    uint64_t addr;
} OMAPI2C;

void omap_i2c_init(OMAPI2C *s, QTestState *qts, uint64_t addr);

typedef struct IMXI2C {
    QOSGraphObject obj;
    I2CAdapter parent;

    uint64_t addr;
} IMXI2C;

void imx_i2c_init(IMXI2C *s, QTestState *qts, uint64_t addr);

#endif
