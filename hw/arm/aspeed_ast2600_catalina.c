/*
 * Facebook Catalina
 *
 * Copyright 2016 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/gpio/pca9552.h"
#include "hw/gpio/pca9554.h"
#include "hw/nvram/eeprom_at24c.h"
#include "hw/sensor/tmp105.h"

#define CATALINA_BMC_HW_STRAP1 0x00002002
#define CATALINA_BMC_HW_STRAP2 0x00000800
#define CATALINA_BMC_RAM_SIZE ASPEED_RAM_SIZE(2 * GiB)

#define TYPE_TMP75 TYPE_TMP105
#define TYPE_TMP421 "tmp421"
#define TYPE_DS1338 "ds1338"

static void catalina_bmc_i2c_init(AspeedMachineState *bmc)
{

    AspeedSoCState *soc = bmc->soc;
    I2CBus *i2c[16] = {};
    I2CSlave *i2c_mux;

    for (int i = 0; i < ARRAY_SIZE(i2c); i++) {
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x71);

    i2c_mux = i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x72);

    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 1),
                            TYPE_PCA9552, 0x20);
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x50, 8 * KiB);

    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x73);

    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x75);

    i2c_mux = i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x76);

    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 1),
                            TYPE_PCA9552, 0x21);
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x50, 8 * KiB);

    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x77);


    i2c_mux = i2c_slave_create_simple(i2c[1], TYPE_PCA9548, 0x70);

    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 5), 0x54, 8 * KiB);
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 5), TYPE_TMP75, 0x4f);

    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 6),
                            TYPE_PCA9554, 0x27);
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 6),
                            TYPE_PCA9552, 0x25);
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 6), 0x51, 8 * KiB);

    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 7), 0x53, 8 * KiB);
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 7), TYPE_TMP75, 0x4b);

    i2c_slave_create_simple(i2c[2], TYPE_PCA9552, 0x20);
    i2c_slave_create_simple(i2c[2], TYPE_PCA9552, 0x21);
    i2c_slave_create_simple(i2c[2], TYPE_PCA9552, 0x27);
    at24c_eeprom_init(i2c[2], 0x50, 8 * KiB);
    at24c_eeprom_init(i2c[2], 0x51, 8 * KiB);

    i2c_mux = i2c_slave_create_simple(i2c[5], TYPE_PCA9548, 0x70);
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 6), 0x52, 8 * KiB);

    i2c_slave_create_simple(i2c[6], TYPE_PCA9552, 0x21);
    i2c_slave_create_simple(i2c[6], TYPE_DS1338, 0x6f);

    i2c_slave_create_simple(i2c[9], TYPE_PCA9552, 0x4f);
    i2c_slave_create_simple(i2c[9], TYPE_TMP75, 0x4b);
    at24c_eeprom_init(i2c[9], 0x50, 8 * KiB);
    at24c_eeprom_init(i2c[9], 0x56, 8 * KiB);

    i2c_slave_create_simple(i2c[10], TYPE_TMP421, 0x1f);
    at24c_eeprom_init(i2c[10], 0x50, 8 * KiB);

    at24c_eeprom_init(i2c[12], 0x50, 8 * KiB);

    at24c_eeprom_init(i2c[13], 0x50, 8 * KiB);
    at24c_eeprom_init(i2c[13], 0x54, 256);
    at24c_eeprom_init(i2c[13], 0x55, 256);
    at24c_eeprom_init(i2c[13], 0x57, 256);

    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x10);
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x11);
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x12);
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x13);
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x14);
    i2c_slave_create_simple(i2c[14], TYPE_PCA9552, 0x15);

    i2c_slave_create_simple(i2c[15], TYPE_TMP421, 0x1f);
    at24c_eeprom_init(i2c[15], 0x52, 8 * KiB);
}

static void aspeed_machine_catalina_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Facebook Catalina BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = CATALINA_BMC_HW_STRAP1;
    amc->hw_strap2 = CATALINA_BMC_HW_STRAP2;
    amc->fmc_model = "w25q01jvq";
    amc->spi_model = NULL;
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON;
    amc->i2c_init  = catalina_bmc_i2c_init;
    mc->default_ram_size = CATALINA_BMC_RAM_SIZE;
    aspeed_machine_class_init_cpus_defaults(mc);
    aspeed_machine_ast2600_class_emmc_init(oc);
}

static const TypeInfo aspeed_ast2600_catalina_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("catalina-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_catalina_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_catalina_types)
