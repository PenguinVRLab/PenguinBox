#ifndef HW_ISA_H
#define HW_ISA_H

#include "system/memory.h"
#include "system/ioport.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

#define ISA_NUM_IRQS 16

#define TYPE_ISA_DEVICE "isa-device"
OBJECT_DECLARE_SIMPLE_TYPE(ISADevice, ISA_DEVICE)

#define TYPE_ISA_BUS "ISA"
OBJECT_DECLARE_SIMPLE_TYPE(ISABus, ISA_BUS)

#define TYPE_ISADMA "isa-dma"

typedef struct IsaDmaClass IsaDmaClass;
DECLARE_CLASS_CHECKERS(IsaDmaClass, ISADMA,
                       TYPE_ISADMA)
#define ISADMA(obj) \
    INTERFACE_CHECK(IsaDma, (obj), TYPE_ISADMA)

typedef enum {
    ISADMA_TRANSFER_VERIFY,
    ISADMA_TRANSFER_READ,
    ISADMA_TRANSFER_WRITE,
    ISADMA_TRANSFER_ILLEGAL,
} IsaDmaTransferMode;

typedef int (*IsaDmaTransferHandler)(void *opaque, int nchan, int pos,
                                     int size);

struct IsaDmaClass {
    InterfaceClass parent;

    bool (*has_autoinitialization)(IsaDma *obj, int nchan);
    int (*read_memory)(IsaDma *obj, int nchan, void *buf, int pos, int len);
    int (*write_memory)(IsaDma *obj, int nchan, void *buf, int pos, int len);
    void (*hold_DREQ)(IsaDma *obj, int nchan);
    void (*release_DREQ)(IsaDma *obj, int nchan);
    void (*schedule)(IsaDma *obj);
    void (*register_channel)(IsaDma *obj, int nchan,
                             IsaDmaTransferHandler transfer_handler,
                             void *opaque);
};

struct ISABus {
    BusState parent_obj;

    MemoryRegion *address_space;
    MemoryRegion *address_space_io;
    qemu_irq *irqs_in;
    IsaDma *dma[2];
};

struct ISADevice {
    DeviceState parent_obj;

    int ioport_id;
};

ISABus *isa_bus_new(DeviceState *dev, MemoryRegion *address_space,
                    MemoryRegion *address_space_io, Error **errp);
void isa_bus_register_input_irqs(ISABus *bus, qemu_irq *irqs_in);
void isa_bus_dma(ISABus *bus, IsaDma *dma8, IsaDma *dma16);
IsaDma *isa_bus_get_dma(ISABus *bus, int nchan);
qemu_irq isa_bus_get_irq(ISABus *bus, unsigned irqnum);
ISADevice *isa_new(const char *name);
ISADevice *isa_try_new(const char *name);
bool isa_realize_and_unref(ISADevice *dev, ISABus *bus, Error **errp);
ISADevice *isa_create_simple(ISABus *bus, const char *name);

ISADevice *isa_vga_init(ISABus *bus);

qemu_irq isa_get_irq(ISADevice *dev, unsigned isairq);
void isa_connect_gpio_out(ISADevice *isadev, int gpioirq, unsigned isairq);
MemoryRegion *isa_address_space(ISADevice *dev);
MemoryRegion *isa_address_space_io(ISADevice *dev);
ISABus *isa_bus_from_device(ISADevice *dev);

void isa_register_ioport(ISADevice *dev, MemoryRegion *io, uint16_t start);

void isa_unregister_ioport(ISADevice *dev, MemoryRegion *io);

int isa_register_portio_list(ISADevice *dev,
                             PortioList *piolist,
                             uint16_t start,
                             const MemoryRegionPortio *portio,
                             void *opaque, const char *name);

#endif
