/*
 * Abstract virtio based memory device
 *
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-md-pci.h"
#include "hw/mem/memory-device.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

void virtio_md_pci_pre_plug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp)
{
    DeviceState *dev = DEVICE(vmd);
    HotplugHandler *bus_handler = qdev_get_bus_hotplug_handler(dev);
    MemoryDeviceState *md = MEMORY_DEVICE(vmd);
    Error *local_err = NULL;

    if (!bus_handler && dev->hotplugged) {
        error_setg(errp, "hotplug of virtio based memory devices not supported"
                   " on this bus.");
        return;
    }
    memory_device_pre_plug(md, ms, &local_err);
    if (!local_err && bus_handler) {
        hotplug_handler_pre_plug(bus_handler, dev, &local_err);
    }
    error_propagate(errp, local_err);
}

void virtio_md_pci_plug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp)
{
    DeviceState *dev = DEVICE(vmd);
    HotplugHandler *bus_handler = qdev_get_bus_hotplug_handler(dev);
    MemoryDeviceState *md = MEMORY_DEVICE(vmd);
    Error *local_err = NULL;

    memory_device_plug(md, ms);
    if (bus_handler) {
        hotplug_handler_plug(bus_handler, dev, &local_err);
        if (local_err) {
            memory_device_unplug(md, ms);
        }
    }
    error_propagate(errp, local_err);
}

void virtio_md_pci_unplug_request(VirtIOMDPCI *vmd, MachineState *ms,
                                  Error **errp)
{
    VirtIOMDPCIClass *vmdc = VIRTIO_MD_PCI_GET_CLASS(vmd);
    DeviceState *dev = DEVICE(vmd);
    HotplugHandler *bus_handler = qdev_get_bus_hotplug_handler(dev);
    HotplugHandlerClass *hdc;
    Error *local_err = NULL;

    if (!vmdc->unplug_request_check) {
        error_setg(errp, "this virtio based memory devices cannot be unplugged");
        return;
    }

    if (!bus_handler) {
        error_setg(errp, "hotunplug of virtio based memory devices not"
                   "supported on this bus");
        return;
    }

    vmdc->unplug_request_check(vmd, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    hdc = HOTPLUG_HANDLER_GET_CLASS(bus_handler);
    if (hdc->unplug_request) {
        hotplug_handler_unplug_request(bus_handler, dev, &local_err);
    } else {
        virtio_md_pci_unplug(vmd, ms, &local_err);
        if (!local_err) {
            object_unparent(OBJECT(dev));
        }
    }
}

void virtio_md_pci_unplug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp)
{
    DeviceState *dev = DEVICE(vmd);
    HotplugHandler *bus_handler = qdev_get_bus_hotplug_handler(dev);
    MemoryDeviceState *md = MEMORY_DEVICE(vmd);
    Error *local_err = NULL;

    memory_device_unplug(md, ms);

    if (bus_handler) {
        hotplug_handler_unplug(bus_handler, dev, &local_err);
        if (local_err) {
            memory_device_plug(md, ms);
            error_propagate(errp, local_err);
            return;
        }
    } else {
        warn_report("Unexpected unplug of virtio based memory device");
        qdev_unrealize(dev);
    }
}

static const TypeInfo virtio_md_pci_info = {
    .name = TYPE_VIRTIO_MD_PCI,
    .parent = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOMDPCI),
    .class_size = sizeof(VirtIOMDPCIClass),
    .abstract = true,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void virtio_md_pci_register(void)
{
    type_register_static(&virtio_md_pci_info);
}
type_init(virtio_md_pci_register)
