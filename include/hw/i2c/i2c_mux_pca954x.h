#ifndef QEMU_I2C_MUX_PCA954X_H
#define QEMU_I2C_MUX_PCA954X_H

#include "hw/i2c/i2c.h"

#define TYPE_PCA9546 "pca9546"
#define TYPE_PCA9548 "pca9548"

I2CBus *pca954x_i2c_get_bus(I2CSlave *mux, uint8_t channel);

#endif
