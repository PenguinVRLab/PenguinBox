/*
 * QEMU Xen PVH machine - common code.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XEN_PVH_COMMON_H__
#define XEN_PVH_COMMON_H__

#include "system/memory.h"
#include "qom/object.h"
#include "hw/boards.h"
#include "hw/pci-host/gpex.h"
#include "hw/xen/xen-hvm-common.h"

#define TYPE_XEN_PVH_MACHINE MACHINE_TYPE_NAME("xen-pvh-base")
OBJECT_DECLARE_TYPE(XenPVHMachineState, XenPVHMachineClass,
                    XEN_PVH_MACHINE)

struct XenPVHMachineClass {
    MachineClass parent;

    void (*init)(MachineState *state);

    void (*set_pci_intx_irq)(void *opaque, int irq, int level);

    int (*set_pci_link_route)(uint8_t line, uint8_t irq);

    uint8_t handle_bufioreq;

    bool has_pci;
    bool has_tpm;
    bool has_virtio_mmio;
};

struct XenPVHMachineState {
    MachineState parent;

    XenIOState ioreq;

    struct {
        MemoryRegion low;
        MemoryRegion high;
    } ram;

    struct {
        GPEXHost gpex;
        MemoryRegion mmio_alias;
        MemoryRegion mmio_high_alias;
    } pci;

    struct {
        MemMapEntry ram_low, ram_high;
        MemMapEntry tpm;

        MemMapEntry virtio_mmio;
        uint32_t virtio_mmio_num;
        uint32_t virtio_mmio_irq_base;

        MemMapEntry pci_ecam, pci_mmio, pci_mmio_high;
        uint32_t pci_intx_irq_base;
    } cfg;
};

void xen_pvh_class_setup_common_props(XenPVHMachineClass *xpc);
#endif
