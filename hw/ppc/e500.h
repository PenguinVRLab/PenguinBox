#ifndef PPCE500_H
#define PPCE500_H

#include "hw/boards.h"
#include "hw/platform-bus.h"
#include "qom/object.h"

#define PLATFORM_CLK_FREQ_HZ (400 * 1000 * 1000)

struct PPCE500MachineState {
    MachineState parent_obj;

    PlatformBusDevice *pbus_dev;
};

struct PPCE500MachineClass {
    MachineClass parent_class;

    void (*fixup_devtree)(void *fdt);

    int pci_first_slot;
    int pci_nr_slots;

    int mpic_version;
    bool has_mpc8xxx_gpio;
    bool has_esdhc;
    hwaddr platform_bus_base;
    hwaddr platform_bus_size;
    int platform_bus_first_irq;
    int platform_bus_num_irqs;
    hwaddr ccsrbar_base;
    hwaddr pci_pio_base;
    hwaddr pci_mmio_base;
    hwaddr pci_mmio_bus_base;
    hwaddr spin_base;
    uint32_t clock_freq;
    uint32_t tb_freq;
};

void ppce500_init(MachineState *machine);

#define TYPE_PPCE500_MACHINE      "ppce500-base-machine"
OBJECT_DECLARE_TYPE(PPCE500MachineState, PPCE500MachineClass, PPCE500_MACHINE)

#endif
