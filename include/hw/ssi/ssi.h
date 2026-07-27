/* QEMU Synchronous Serial Interface support.  */

/*
 * In principle SSI is a point-point interface.  As such the qemu
 * implementation has a single peripheral on a "bus".
 * However it is fairly common for boards to have multiple peripherals
 * connected to a single master, and select devices with an external
 * chip select.  This is implemented in qemu by having an explicit mux device.
 * It is assumed that master and peripheral are both using the same transfer
 * width.
 */

#ifndef QEMU_SSI_H
#define QEMU_SSI_H

#include "hw/qdev-core.h"
#include "qom/object.h"

typedef enum SSICSMode SSICSMode;

#define TYPE_SSI_PERIPHERAL "ssi-peripheral"
OBJECT_DECLARE_TYPE(SSIPeripheral, SSIPeripheralClass,
                    SSI_PERIPHERAL)

#define SSI_GPIO_CS "ssi-gpio-cs"

enum SSICSMode {
    SSI_CS_NONE = 0,
    SSI_CS_LOW,
    SSI_CS_HIGH,
};

struct SSIPeripheralClass {
    DeviceClass parent_class;

    void (*realize)(SSIPeripheral *dev, Error **errp);

    uint32_t (*transfer)(SSIPeripheral *dev, uint32_t val);
    int (*set_cs)(SSIPeripheral *dev, bool select);
    SSICSMode cs_polarity;

    uint32_t (*transfer_raw)(SSIPeripheral *dev, uint32_t val);
};

struct SSIPeripheral {
    DeviceState parent_obj;

    SSIPeripheralClass *spc;

    bool cs;

    uint8_t cs_index;
};

extern const VMStateDescription vmstate_ssi_peripheral;

#define VMSTATE_SSI_PERIPHERAL(_field, _state) {                     \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SSIPeripheral),                             \
    .vmsd       = &vmstate_ssi_peripheral,                           \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, SSIPeripheral), \
}

DeviceState *ssi_create_peripheral(SSIBus *bus, const char *name);
bool ssi_realize_and_unref(DeviceState *dev, SSIBus *bus, Error **errp);

SSIBus *ssi_create_bus(DeviceState *parent, const char *name);

uint32_t ssi_transfer(SSIBus *bus, uint32_t val);

DeviceState *ssi_get_cs(SSIBus *bus, uint8_t cs_index);

#endif
