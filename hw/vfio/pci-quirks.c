/*
 * device quirks for PCI devices
 *
 * Copyright Red Hat, Inc. 2012-2015
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES
#include "exec/memop.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include <sys/ioctl.h>
#include "hw/nvram/fw_cfg.h"
#include "hw/qdev-properties.h"
#include "pci.h"
#include "pci-quirks.h"
#include "trace.h"

static const struct {
    uint32_t vendor;
    uint32_t device;
} rom_denylist[] = {
    { 0x14e4, 0x168e },
};

bool vfio_opt_rom_in_denylist(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0 ; i < ARRAY_SIZE(rom_denylist); i++) {
        if (vfio_pci_is(vdev, rom_denylist[i].vendor, rom_denylist[i].device)) {
            trace_vfio_quirk_rom_in_denylist(vdev->vbasedev.name,
                                             rom_denylist[i].vendor,
                                             rom_denylist[i].device);
            return true;
        }
    }
    return false;
}

static uint64_t vfio_generic_window_quirk_address_read(void *opaque,
                                                       hwaddr addr,
                                                       unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;

    return vfio_region_read(&vdev->bars[window->bar].region,
                            addr + window->address_offset, size);
}

static void vfio_generic_window_quirk_address_write(void *opaque, hwaddr addr,
                                                    uint64_t data,
                                                    unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;
    int i;

    window->window_enabled = false;

    vfio_region_write(&vdev->bars[window->bar].region,
                      addr + window->address_offset, data, size);

    for (i = 0; i < window->nr_matches; i++) {
        if ((data & ~window->matches[i].mask) == window->matches[i].match) {
            window->window_enabled = true;
            window->address_val = data & window->matches[i].mask;
            trace_vfio_quirk_generic_window_address_write(vdev->vbasedev.name,
                                    memory_region_name(window->addr_mem), data);
            break;
        }
    }
}

const MemoryRegionOps vfio_generic_window_address_quirk = {
    .read = vfio_generic_window_quirk_address_read,
    .write = vfio_generic_window_quirk_address_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_generic_window_quirk_data_read(void *opaque,
                                                    hwaddr addr, unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint64_t data;

    data = vfio_region_read(&vdev->bars[window->bar].region,
                            addr + window->data_offset, size);

    if (window->window_enabled) {
        data = vfio_pci_read_config(pdev, window->address_val, size);
        trace_vfio_quirk_generic_window_data_read(vdev->vbasedev.name,
                                    memory_region_name(window->data_mem), data);
    }

    return data;
}

static void vfio_generic_window_quirk_data_write(void *opaque, hwaddr addr,
                                                 uint64_t data, unsigned size)
{
    VFIOConfigWindowQuirk *window = opaque;
    VFIOPCIDevice *vdev = window->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if (window->window_enabled) {
        vfio_pci_write_config(pdev, window->address_val, data, size);
        trace_vfio_quirk_generic_window_data_write(vdev->vbasedev.name,
                                    memory_region_name(window->data_mem), data);
        return;
    }

    vfio_region_write(&vdev->bars[window->bar].region,
                      addr + window->data_offset, data, size);
}

const MemoryRegionOps vfio_generic_window_data_quirk = {
    .read = vfio_generic_window_quirk_data_read,
    .write = vfio_generic_window_quirk_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_generic_quirk_mirror_read(void *opaque,
                                               hwaddr addr, unsigned size)
{
    VFIOConfigMirrorQuirk *mirror = opaque;
    VFIOPCIDevice *vdev = mirror->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint64_t data;

    (void)vfio_region_read(&vdev->bars[mirror->bar].region,
                           addr + mirror->offset, size);

    addr += mirror->config_offset;
    data = vfio_pci_read_config(pdev, addr, size);
    trace_vfio_quirk_generic_mirror_read(vdev->vbasedev.name,
                                         memory_region_name(mirror->mem),
                                         addr, data);
    return data;
}

static void vfio_generic_quirk_mirror_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned size)
{
    VFIOConfigMirrorQuirk *mirror = opaque;
    VFIOPCIDevice *vdev = mirror->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);

    addr += mirror->config_offset;
    vfio_pci_write_config(pdev, addr, data, size);
    trace_vfio_quirk_generic_mirror_write(vdev->vbasedev.name,
                                          memory_region_name(mirror->mem),
                                          addr, data);
}

const MemoryRegionOps vfio_generic_mirror_quirk = {
    .read = vfio_generic_quirk_mirror_read,
    .write = vfio_generic_quirk_mirror_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static bool vfio_range_contained(uint64_t first1, uint64_t len1,
                                 uint64_t first2, uint64_t len2) {
    return (first1 >= first2 && first1 + len1 <= first2 + len2);
}

#define PCI_VENDOR_ID_ATI               0x1002

static uint64_t vfio_ati_3c3_quirk_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    VFIOPCIDevice *vdev = opaque;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint64_t data = vfio_pci_read_config(pdev,
                                         PCI_BASE_ADDRESS_4 + 1, size);

    trace_vfio_quirk_ati_3c3_read(vdev->vbasedev.name, data);

    return data;
}

static void vfio_ati_3c3_quirk_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid access\n", __func__);
}

static const MemoryRegionOps vfio_ati_3c3_quirk = {
    .read = vfio_ati_3c3_quirk_read,
    .write = vfio_ati_3c3_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

VFIOQuirk *vfio_quirk_alloc(int nr_mem)
{
    VFIOQuirk *quirk = g_new0(VFIOQuirk, 1);
    QLIST_INIT(&quirk->ioeventfds);
    quirk->mem = g_new0(MemoryRegion, nr_mem);
    quirk->nr_mem = nr_mem;

    return quirk;
}

static void vfio_ioeventfd_exit(VFIOPCIDevice *vdev, VFIOIOEventFD *ioeventfd)
{
    QLIST_REMOVE(ioeventfd, next);
    memory_region_del_eventfd(ioeventfd->mr, ioeventfd->addr, ioeventfd->size,
                              true, ioeventfd->data, &ioeventfd->e);

    if (ioeventfd->vfio) {
        struct vfio_device_ioeventfd vfio_ioeventfd;

        vfio_ioeventfd.argsz = sizeof(vfio_ioeventfd);
        vfio_ioeventfd.flags = ioeventfd->size;
        vfio_ioeventfd.data = ioeventfd->data;
        vfio_ioeventfd.offset = ioeventfd->region->fd_offset +
                                ioeventfd->region_addr;
        vfio_ioeventfd.fd = -1;

        if (ioctl(vdev->vbasedev.fd, VFIO_DEVICE_IOEVENTFD, &vfio_ioeventfd)) {
            error_report("Failed to remove vfio ioeventfd for %s+0x%"
                         HWADDR_PRIx"[%d]:0x%"PRIx64" (%m)",
                         memory_region_name(ioeventfd->mr), ioeventfd->addr,
                         ioeventfd->size, ioeventfd->data);
        }
    } else {
        qemu_set_fd_handler(event_notifier_get_fd(&ioeventfd->e),
                            NULL, NULL, NULL);
    }

    event_notifier_cleanup(&ioeventfd->e);
    trace_vfio_ioeventfd_exit(memory_region_name(ioeventfd->mr),
                              (uint64_t)ioeventfd->addr, ioeventfd->size,
                              ioeventfd->data);
    g_free(ioeventfd);
}

static void vfio_drop_dynamic_eventfds(VFIOPCIDevice *vdev, VFIOQuirk *quirk)
{
    VFIOIOEventFD *ioeventfd, *tmp;

    QLIST_FOREACH_SAFE(ioeventfd, &quirk->ioeventfds, next, tmp) {
        if (ioeventfd->dynamic) {
            vfio_ioeventfd_exit(vdev, ioeventfd);
        }
    }
}

static void vfio_ioeventfd_handler(void *opaque)
{
    VFIOIOEventFD *ioeventfd = opaque;

    if (event_notifier_test_and_clear(&ioeventfd->e)) {
        vfio_region_write(ioeventfd->region, ioeventfd->region_addr,
                          ioeventfd->data, ioeventfd->size);
        trace_vfio_ioeventfd_handler(memory_region_name(ioeventfd->mr),
                                     (uint64_t)ioeventfd->addr, ioeventfd->size,
                                     ioeventfd->data);
    }
}

static VFIOIOEventFD *vfio_ioeventfd_init(VFIOPCIDevice *vdev,
                                          MemoryRegion *mr, hwaddr addr,
                                          unsigned size, uint64_t data,
                                          VFIORegion *region,
                                          hwaddr region_addr, bool dynamic)
{
    VFIOIOEventFD *ioeventfd;

    if (vdev->no_kvm_ioeventfd) {
        return NULL;
    }

    ioeventfd = g_malloc0(sizeof(*ioeventfd));

    if (event_notifier_init(&ioeventfd->e, 0)) {
        g_free(ioeventfd);
        return NULL;
    }

    ioeventfd->mr = mr;
    ioeventfd->addr = addr;
    ioeventfd->size = size;
    ioeventfd->data = data;
    ioeventfd->dynamic = dynamic;
    ioeventfd->region = region;
    ioeventfd->region_addr = region_addr;

    if (!vdev->no_vfio_ioeventfd) {
        struct vfio_device_ioeventfd vfio_ioeventfd;

        vfio_ioeventfd.argsz = sizeof(vfio_ioeventfd);
        vfio_ioeventfd.flags = ioeventfd->size;
        vfio_ioeventfd.data = ioeventfd->data;
        vfio_ioeventfd.offset = ioeventfd->region->fd_offset +
                                ioeventfd->region_addr;
        vfio_ioeventfd.fd = event_notifier_get_fd(&ioeventfd->e);

        ioeventfd->vfio = !ioctl(vdev->vbasedev.fd,
                                 VFIO_DEVICE_IOEVENTFD, &vfio_ioeventfd);
    }

    if (!ioeventfd->vfio) {
        qemu_set_fd_handler(event_notifier_get_fd(&ioeventfd->e),
                            vfio_ioeventfd_handler, NULL, ioeventfd);
    }

    memory_region_add_eventfd(ioeventfd->mr, ioeventfd->addr, ioeventfd->size,
                              true, ioeventfd->data, &ioeventfd->e);
    trace_vfio_ioeventfd_init(memory_region_name(mr), (uint64_t)addr,
                              size, data, ioeventfd->vfio);

    return ioeventfd;
}

static void vfio_vga_probe_ati_3c3_quirk(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_ATI, PCI_ANY_ID) ||
        !vdev->bars[4].ioport || vdev->bars[4].region.size < 256) {
        return;
    }

    quirk = vfio_quirk_alloc(1);

    memory_region_init_io(quirk->mem, OBJECT(vdev), &vfio_ati_3c3_quirk, vdev,
                          "vfio-ati-3c3-quirk", 1);
    memory_region_add_subregion(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                                3 , quirk->mem);

    QLIST_INSERT_HEAD(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].quirks,
                      quirk, next);

    trace_vfio_quirk_ati_3c3_probe(vdev->vbasedev.name);
}

static void vfio_probe_ati_bar4_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOConfigWindowQuirk *window;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_ATI, PCI_ANY_ID) ||
        !vdev->vga || nr != 4 || !vdev->bars[4].ioport) {
        return;
    }

    quirk = vfio_quirk_alloc(2);
    window = quirk->data = g_malloc0(sizeof(*window) +
                                     sizeof(VFIOConfigWindowMatch));
    window->vdev = vdev;
    window->address_offset = 0;
    window->data_offset = 4;
    window->nr_matches = 1;
    window->matches[0].match = 0x4000;
    window->matches[0].mask = vdev->config_size - 1;
    window->bar = nr;
    window->addr_mem = &quirk->mem[0];
    window->data_mem = &quirk->mem[1];

    memory_region_init_io(window->addr_mem, OBJECT(vdev),
                          &vfio_generic_window_address_quirk, window,
                          "vfio-ati-bar4-window-address-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->address_offset,
                                        window->addr_mem, 1);

    memory_region_init_io(window->data_mem, OBJECT(vdev),
                          &vfio_generic_window_data_quirk, window,
                          "vfio-ati-bar4-window-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->data_offset,
                                        window->data_mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_ati_bar4_probe(vdev->vbasedev.name);
}

static void vfio_probe_ati_bar2_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOConfigMirrorQuirk *mirror;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_ATI, PCI_ANY_ID) ||
        !vdev->vga || nr != 2 || !vdev->bars[2].mem64) {
        return;
    }

    quirk = vfio_quirk_alloc(1);
    mirror = quirk->data = g_malloc0(sizeof(*mirror));
    mirror->mem = quirk->mem;
    mirror->vdev = vdev;
    mirror->offset = 0x4000;
    mirror->bar = nr;

    memory_region_init_io(mirror->mem, OBJECT(vdev),
                          &vfio_generic_mirror_quirk, mirror,
                          "vfio-ati-bar2-4000-quirk", PCI_CONFIG_SPACE_SIZE);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        mirror->offset, mirror->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_ati_bar2_probe(vdev->vbasedev.name);
}

typedef enum {NONE = 0, SELECT, WINDOW, READ, WRITE} VFIONvidia3d0State;
static const char *nv3d0_states[] = { "NONE", "SELECT",
                                      "WINDOW", "READ", "WRITE" };

typedef struct VFIONvidia3d0Quirk {
    VFIOPCIDevice *vdev;
    VFIONvidia3d0State state;
    uint32_t offset;
} VFIONvidia3d0Quirk;

static uint64_t vfio_nvidia_3d4_quirk_read(void *opaque,
                                           hwaddr addr, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;

    quirk->state = NONE;

    return vfio_vga_read(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
                         addr + 0x14, size);
}

static void vfio_nvidia_3d4_quirk_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    VFIONvidia3d0State old_state = quirk->state;

    quirk->state = NONE;

    switch (data) {
    case 0x338:
        if (old_state == NONE) {
            quirk->state = SELECT;
            trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                              nv3d0_states[quirk->state]);
        }
        break;
    case 0x538:
        if (old_state == WINDOW) {
            quirk->state = READ;
            trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                              nv3d0_states[quirk->state]);
        }
        break;
    case 0x738:
        if (old_state == WINDOW) {
            quirk->state = WRITE;
            trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                              nv3d0_states[quirk->state]);
        }
        break;
    }

    vfio_vga_write(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
                   addr + 0x14, data, size);
}

static const MemoryRegionOps vfio_nvidia_3d4_quirk = {
    .read = vfio_nvidia_3d4_quirk_read,
    .write = vfio_nvidia_3d4_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_nvidia_3d0_quirk_read(void *opaque,
                                           hwaddr addr, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIONvidia3d0State old_state = quirk->state;
    uint64_t data = vfio_vga_read(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
                                  addr + 0x10, size);

    quirk->state = NONE;

    if (old_state == READ &&
        (quirk->offset & ~(PCI_CONFIG_SPACE_SIZE - 1)) == 0x1800) {
        uint8_t offset = quirk->offset & (PCI_CONFIG_SPACE_SIZE - 1);

        data = vfio_pci_read_config(pdev, offset, size);
        trace_vfio_quirk_nvidia_3d0_read(vdev->vbasedev.name,
                                         offset, size, data);
    }

    return data;
}

static void vfio_nvidia_3d0_quirk_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    VFIONvidia3d0Quirk *quirk = opaque;
    VFIOPCIDevice *vdev = quirk->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIONvidia3d0State old_state = quirk->state;

    quirk->state = NONE;

    if (old_state == SELECT) {
        quirk->offset = (uint32_t)data;
        quirk->state = WINDOW;
        trace_vfio_quirk_nvidia_3d0_state(vdev->vbasedev.name,
                                          nv3d0_states[quirk->state]);
    } else if (old_state == WRITE) {
        if ((quirk->offset & ~(PCI_CONFIG_SPACE_SIZE - 1)) == 0x1800) {
            uint8_t offset = quirk->offset & (PCI_CONFIG_SPACE_SIZE - 1);

            vfio_pci_write_config(pdev, offset, data, size);
            trace_vfio_quirk_nvidia_3d0_write(vdev->vbasedev.name,
                                              offset, data, size);
            return;
        }
    }

    vfio_vga_write(&vdev->vga->region[QEMU_PCI_VGA_IO_HI],
                   addr + 0x10, data, size);
}

static const MemoryRegionOps vfio_nvidia_3d0_quirk = {
    .read = vfio_nvidia_3d0_quirk_read,
    .write = vfio_nvidia_3d0_quirk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_vga_probe_nvidia_3d0_quirk(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;
    VFIONvidia3d0Quirk *data;

    if (vdev->no_geforce_quirks ||
        !vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID) ||
        !vdev->bars[1].region.size) {
        return;
    }

    quirk = vfio_quirk_alloc(2);
    quirk->data = data = g_malloc0(sizeof(*data));
    data->vdev = vdev;

    memory_region_init_io(&quirk->mem[0], OBJECT(vdev), &vfio_nvidia_3d4_quirk,
                          data, "vfio-nvidia-3d4-quirk", 2);
    memory_region_add_subregion(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                                0x14 , &quirk->mem[0]);

    memory_region_init_io(&quirk->mem[1], OBJECT(vdev), &vfio_nvidia_3d0_quirk,
                          data, "vfio-nvidia-3d0-quirk", 2);
    memory_region_add_subregion(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                                0x10 , &quirk->mem[1]);

    QLIST_INSERT_HEAD(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].quirks,
                      quirk, next);

    trace_vfio_quirk_nvidia_3d0_probe(vdev->vbasedev.name);
}

typedef struct VFIONvidiaBAR5Quirk {
    uint32_t master;
    uint32_t enable;
    MemoryRegion *addr_mem;
    MemoryRegion *data_mem;
    bool enabled;
    VFIOConfigWindowQuirk window;
} VFIONvidiaBAR5Quirk;

static void vfio_nvidia_bar5_enable(VFIONvidiaBAR5Quirk *bar5)
{
    VFIOPCIDevice *vdev = bar5->window.vdev;

    if (((bar5->master & bar5->enable) & 0x1) == bar5->enabled) {
        return;
    }

    bar5->enabled = !bar5->enabled;
    trace_vfio_quirk_nvidia_bar5_state(vdev->vbasedev.name,
                                       bar5->enabled ?  "Enable" : "Disable");
    memory_region_set_enabled(bar5->addr_mem, bar5->enabled);
    memory_region_set_enabled(bar5->data_mem, bar5->enabled);
}

static uint64_t vfio_nvidia_bar5_quirk_master_read(void *opaque,
                                                   hwaddr addr, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    return vfio_region_read(&vdev->bars[5].region, addr, size);
}

static void vfio_nvidia_bar5_quirk_master_write(void *opaque, hwaddr addr,
                                                uint64_t data, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    vfio_region_write(&vdev->bars[5].region, addr, data, size);

    bar5->master = data;
    vfio_nvidia_bar5_enable(bar5);
}

static const MemoryRegionOps vfio_nvidia_bar5_quirk_master = {
    .read = vfio_nvidia_bar5_quirk_master_read,
    .write = vfio_nvidia_bar5_quirk_master_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_nvidia_bar5_quirk_enable_read(void *opaque,
                                                   hwaddr addr, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    return vfio_region_read(&vdev->bars[5].region, addr + 4, size);
}

static void vfio_nvidia_bar5_quirk_enable_write(void *opaque, hwaddr addr,
                                                uint64_t data, unsigned size)
{
    VFIONvidiaBAR5Quirk *bar5 = opaque;
    VFIOPCIDevice *vdev = bar5->window.vdev;

    vfio_region_write(&vdev->bars[5].region, addr + 4, data, size);

    bar5->enable = data;
    vfio_nvidia_bar5_enable(bar5);
}

static const MemoryRegionOps vfio_nvidia_bar5_quirk_enable = {
    .read = vfio_nvidia_bar5_quirk_enable_read,
    .write = vfio_nvidia_bar5_quirk_enable_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_nvidia_bar5_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIONvidiaBAR5Quirk *bar5;
    VFIOConfigWindowQuirk *window;

    if (vdev->no_geforce_quirks ||
        !vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID) ||
        !vdev->vga || nr != 5 || !vdev->bars[5].ioport) {
        return;
    }

    quirk = vfio_quirk_alloc(4);
    bar5 = quirk->data = g_malloc0(sizeof(*bar5) +
                                   (sizeof(VFIOConfigWindowMatch) * 2));
    window = &bar5->window;

    window->vdev = vdev;
    window->address_offset = 0x8;
    window->data_offset = 0xc;
    window->nr_matches = 2;
    window->matches[0].match = 0x1800;
    window->matches[0].mask = PCI_CONFIG_SPACE_SIZE - 1;
    window->matches[1].match = 0x88000;
    window->matches[1].mask = vdev->config_size - 1;
    window->bar = nr;
    window->addr_mem = bar5->addr_mem = &quirk->mem[0];
    window->data_mem = bar5->data_mem = &quirk->mem[1];

    memory_region_init_io(window->addr_mem, OBJECT(vdev),
                          &vfio_generic_window_address_quirk, window,
                          "vfio-nvidia-bar5-window-address-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->address_offset,
                                        window->addr_mem, 1);
    memory_region_set_enabled(window->addr_mem, false);

    memory_region_init_io(window->data_mem, OBJECT(vdev),
                          &vfio_generic_window_data_quirk, window,
                          "vfio-nvidia-bar5-window-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        window->data_offset,
                                        window->data_mem, 1);
    memory_region_set_enabled(window->data_mem, false);

    memory_region_init_io(&quirk->mem[2], OBJECT(vdev),
                          &vfio_nvidia_bar5_quirk_master, bar5,
                          "vfio-nvidia-bar5-master-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0, &quirk->mem[2], 1);

    memory_region_init_io(&quirk->mem[3], OBJECT(vdev),
                          &vfio_nvidia_bar5_quirk_enable, bar5,
                          "vfio-nvidia-bar5-enable-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        4, &quirk->mem[3], 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_nvidia_bar5_probe(vdev->vbasedev.name);
}

typedef struct LastDataSet {
    VFIOQuirk *quirk;
    hwaddr addr;
    uint64_t data;
    unsigned size;
    int hits;
    int added;
} LastDataSet;

#define MAX_DYN_IOEVENTFD 10
#define HITS_FOR_IOEVENTFD 10

static void vfio_nvidia_quirk_mirror_write(void *opaque, hwaddr addr,
                                           uint64_t data, unsigned size)
{
    VFIOConfigMirrorQuirk *mirror = opaque;
    VFIOPCIDevice *vdev = mirror->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    LastDataSet *last = (LastDataSet *)&mirror->data;

    vfio_generic_quirk_mirror_write(opaque, addr, data, size);

    if ((pdev->cap_present & QEMU_PCI_CAP_MSI) &&
        vfio_range_contained(addr, size, pdev->msi_cap, PCI_MSI_FLAGS)) {
        vfio_region_write(&vdev->bars[mirror->bar].region,
                          addr + mirror->offset, data, size);
        trace_vfio_quirk_nvidia_bar0_msi_ack(vdev->vbasedev.name);
    }

    if (!vdev->no_kvm_ioeventfd &&
        addr >= PCI_STD_HEADER_SIZEOF && last->added <= MAX_DYN_IOEVENTFD) {
        if (addr != last->addr || data != last->data || size != last->size) {
            last->addr = addr;
            last->data = data;
            last->size = size;
            last->hits = 1;
        } else if (++last->hits >= HITS_FOR_IOEVENTFD) {
            if (last->added < MAX_DYN_IOEVENTFD) {
                VFIOIOEventFD *ioeventfd;
                ioeventfd = vfio_ioeventfd_init(vdev, mirror->mem, addr, size,
                                        data, &vdev->bars[mirror->bar].region,
                                        mirror->offset + addr, true);
                if (ioeventfd) {
                    VFIOQuirk *quirk = last->quirk;

                    QLIST_INSERT_HEAD(&quirk->ioeventfds, ioeventfd, next);
                    last->added++;
                }
            } else {
                last->added++;
                warn_report("NVIDIA ioeventfd queue full for %s, unable to "
                            "accelerate 0x%"HWADDR_PRIx", data 0x%"PRIx64", "
                            "size %u", vdev->vbasedev.name, addr, data, size);
            }
        }
    }
}

static const MemoryRegionOps vfio_nvidia_mirror_quirk = {
    .read = vfio_generic_quirk_mirror_read,
    .write = vfio_nvidia_quirk_mirror_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_nvidia_bar0_quirk_reset(VFIOPCIDevice *vdev, VFIOQuirk *quirk)
{
    VFIOConfigMirrorQuirk *mirror = quirk->data;
    LastDataSet *last = (LastDataSet *)&mirror->data;

    last->addr = last->data = last->size = last->hits = last->added = 0;

    vfio_drop_dynamic_eventfds(vdev, quirk);
}

static void vfio_probe_nvidia_bar0_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOConfigMirrorQuirk *mirror;
    LastDataSet *last;

    if (vdev->no_geforce_quirks ||
        !vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID) ||
        !vfio_is_vga(vdev) || nr != 0) {
        return;
    }

    quirk = vfio_quirk_alloc(1);
    quirk->reset = vfio_nvidia_bar0_quirk_reset;
    mirror = quirk->data = g_malloc0(sizeof(*mirror) + sizeof(LastDataSet));
    mirror->mem = quirk->mem;
    mirror->vdev = vdev;
    mirror->offset = 0x88000;
    mirror->bar = nr;
    last = (LastDataSet *)&mirror->data;
    last->quirk = quirk;

    memory_region_init_io(mirror->mem, OBJECT(vdev),
                          &vfio_nvidia_mirror_quirk, mirror,
                          "vfio-nvidia-bar0-88000-mirror-quirk",
                          vdev->config_size);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        mirror->offset, mirror->mem, 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    if (vdev->vga) {
        quirk = vfio_quirk_alloc(1);
        quirk->reset = vfio_nvidia_bar0_quirk_reset;
        mirror = quirk->data = g_malloc0(sizeof(*mirror) + sizeof(LastDataSet));
        mirror->mem = quirk->mem;
        mirror->vdev = vdev;
        mirror->offset = 0x1800;
        mirror->bar = nr;
        last = (LastDataSet *)&mirror->data;
        last->quirk = quirk;

        memory_region_init_io(mirror->mem, OBJECT(vdev),
                              &vfio_nvidia_mirror_quirk, mirror,
                              "vfio-nvidia-bar0-1800-mirror-quirk",
                              PCI_CONFIG_SPACE_SIZE);
        memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                            mirror->offset, mirror->mem, 1);

        QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);
    }

    trace_vfio_quirk_nvidia_bar0_probe(vdev->vbasedev.name);
}

#define PCI_VENDOR_ID_REALTEK 0x10ec

typedef struct VFIOrtl8168Quirk {
    VFIOPCIDevice *vdev;
    uint32_t addr;
    uint32_t data;
    bool enabled;
} VFIOrtl8168Quirk;

static uint64_t vfio_rtl8168_quirk_address_read(void *opaque,
                                                hwaddr addr, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;
    uint64_t data = vfio_region_read(&vdev->bars[2].region, addr + 0x74, size);

    if (rtl->enabled) {
        data = rtl->addr ^ 0x80000000U;
        trace_vfio_quirk_rtl8168_fake_latch(vdev->vbasedev.name, data);
    }

    return data;
}

static void vfio_rtl8168_quirk_address_write(void *opaque, hwaddr addr,
                                             uint64_t data, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);

    rtl->enabled = false;

    if ((data & 0x7fff0000) == 0x10000) {
        rtl->enabled = true;
        rtl->addr = (uint32_t)data;

        if (data & 0x80000000U) {
            if (pdev->cap_present & QEMU_PCI_CAP_MSIX) {
                hwaddr offset = data & 0xfff;
                uint64_t val = rtl->data;

                trace_vfio_quirk_rtl8168_msix_write(vdev->vbasedev.name,
                                                    (uint16_t)offset, val);

                memory_region_dispatch_write(&pdev->msix_table_mmio,
                                             offset, val,
                                             size_memop(size) | MO_LE,
                                             MEMTXATTRS_UNSPECIFIED);
            }
            return;
        }
    }

    vfio_region_write(&vdev->bars[2].region, addr + 0x74, data, size);
}

static const MemoryRegionOps vfio_rtl_address_quirk = {
    .read = vfio_rtl8168_quirk_address_read,
    .write = vfio_rtl8168_quirk_address_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t vfio_rtl8168_quirk_data_read(void *opaque,
                                             hwaddr addr, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint64_t data = vfio_region_read(&vdev->bars[2].region, addr + 0x70, size);

    if (rtl->enabled && (pdev->cap_present & QEMU_PCI_CAP_MSIX)) {
        hwaddr offset = rtl->addr & 0xfff;
        memory_region_dispatch_read(&pdev->msix_table_mmio, offset,
                                    &data, size_memop(size) | MO_LE,
                                    MEMTXATTRS_UNSPECIFIED);
        trace_vfio_quirk_rtl8168_msix_read(vdev->vbasedev.name, offset, data);
    }

    return data;
}

static void vfio_rtl8168_quirk_data_write(void *opaque, hwaddr addr,
                                          uint64_t data, unsigned size)
{
    VFIOrtl8168Quirk *rtl = opaque;
    VFIOPCIDevice *vdev = rtl->vdev;

    rtl->data = (uint32_t)data;

    vfio_region_write(&vdev->bars[2].region, addr + 0x70, data, size);
}

static const MemoryRegionOps vfio_rtl_data_quirk = {
    .read = vfio_rtl8168_quirk_data_read,
    .write = vfio_rtl8168_quirk_data_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_probe_rtl8168_bar2_quirk(VFIOPCIDevice *vdev, int nr)
{
    VFIOQuirk *quirk;
    VFIOrtl8168Quirk *rtl;

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_REALTEK, 0x8168) || nr != 2) {
        return;
    }

    quirk = vfio_quirk_alloc(2);
    quirk->data = rtl = g_malloc0(sizeof(*rtl));
    rtl->vdev = vdev;

    memory_region_init_io(&quirk->mem[0], OBJECT(vdev),
                          &vfio_rtl_address_quirk, rtl,
                          "vfio-rtl8168-window-address-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0x74, &quirk->mem[0], 1);

    memory_region_init_io(&quirk->mem[1], OBJECT(vdev),
                          &vfio_rtl_data_quirk, rtl,
                          "vfio-rtl8168-window-data-quirk", 4);
    memory_region_add_subregion_overlap(vdev->bars[nr].region.mem,
                                        0x70, &quirk->mem[1], 1);

    QLIST_INSERT_HEAD(&vdev->bars[nr].quirks, quirk, next);

    trace_vfio_quirk_rtl8168_probe(vdev->vbasedev.name);
}

bool vfio_config_quirk_setup(VFIOPCIDevice *vdev, Error **errp)
{
#ifdef CONFIG_VFIO_IGD
    if (!vfio_probe_igd_config_quirk(vdev, errp)) {
        return false;
    }
#endif
    return true;
}

void vfio_vga_quirk_setup(VFIOPCIDevice *vdev)
{
    vfio_vga_probe_ati_3c3_quirk(vdev);
    vfio_vga_probe_nvidia_3d0_quirk(vdev);
}

void vfio_vga_quirk_exit(VFIOPCIDevice *vdev)
{
    VFIOQuirk *quirk;
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vdev->vga->region); i++) {
        QLIST_FOREACH(quirk, &vdev->vga->region[i].quirks, next) {
            for (j = 0; j < quirk->nr_mem; j++) {
                memory_region_del_subregion(&vdev->vga->region[i].mem,
                                            &quirk->mem[j]);
            }
        }
    }
}

void vfio_vga_quirk_finalize(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(vdev->vga->region); i++) {
        while (!QLIST_EMPTY(&vdev->vga->region[i].quirks)) {
            VFIOQuirk *quirk = QLIST_FIRST(&vdev->vga->region[i].quirks);
            QLIST_REMOVE(quirk, next);
            g_free(quirk->mem);
            g_free(quirk->data);
            g_free(quirk);
        }
    }
}

void vfio_bar_quirk_setup(VFIOPCIDevice *vdev, int nr)
{
    vfio_probe_ati_bar4_quirk(vdev, nr);
    vfio_probe_ati_bar2_quirk(vdev, nr);
    vfio_probe_nvidia_bar5_quirk(vdev, nr);
    vfio_probe_nvidia_bar0_quirk(vdev, nr);
    vfio_probe_rtl8168_bar2_quirk(vdev, nr);
#ifdef CONFIG_VFIO_IGD
    vfio_probe_igd_bar0_quirk(vdev, nr);
#endif
}

void vfio_bar_quirk_exit(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];
    VFIOQuirk *quirk;
    int i;

    QLIST_FOREACH(quirk, &bar->quirks, next) {
        while (!QLIST_EMPTY(&quirk->ioeventfds)) {
            vfio_ioeventfd_exit(vdev, QLIST_FIRST(&quirk->ioeventfds));
        }

        for (i = 0; i < quirk->nr_mem; i++) {
            memory_region_del_subregion(bar->region.mem, &quirk->mem[i]);
        }
    }
}

void vfio_bar_quirk_finalize(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];

    while (!QLIST_EMPTY(&bar->quirks)) {
        VFIOQuirk *quirk = QLIST_FIRST(&bar->quirks);
        QLIST_REMOVE(quirk, next);
        g_free(quirk->mem);
        g_free(quirk->data);
        g_free(quirk);
    }
}

void vfio_quirk_reset(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        VFIOQuirk *quirk;
        VFIOBAR *bar = &vdev->bars[i];

        QLIST_FOREACH(quirk, &bar->quirks, next) {
            if (quirk->reset) {
                quirk->reset(vdev, quirk);
            }
        }
    }
}

/*
 * AMD Radeon PCI config reset, based on Linux:
 *   drivers/gpu/drm/radeon/ci_smc.c:ci_is_smc_running()
 *   drivers/gpu/drm/radeon/radeon_device.c:radeon_pci_config_reset
 *   drivers/gpu/drm/radeon/ci_smc.c:ci_reset_smc()
 *   drivers/gpu/drm/radeon/ci_smc.c:ci_stop_smc_clock()
 * IDs: include/drm/drm_pciids.h
 * Registers: http://cgit.freedesktop.org/~agd5f/linux/commit/?id=4e2aa447f6f0
 *
 * Bonaire and Hawaii GPUs do not respond to a bus reset.  This is a bug in the
 * hardware that should be fixed on future ASICs.  The symptom of this is that
 * once the accerlated driver loads, Windows guests will bsod on subsequent
 * attmpts to load the driver, such as after VM reset or shutdown/restart.  To
 * work around this, we do an AMD specific PCI config reset, followed by an SMC
 * reset.  The PCI config reset only works if SMC firmware is running, so we
 * have a dependency on the state of the device as to whether this reset will
 * be effective.  There are still cases where we won't be able to kick the
 * device into working, but this greatly improves the usability overall.  The
 * config reset magic is relatively common on AMD GPUs, but the setup and SMC
 * poking is largely ASIC specific.
 */
static bool vfio_radeon_smc_is_running(VFIOPCIDevice *vdev)
{
    uint32_t clk, pc_c;

    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000004, 4);
    clk = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000370, 4);
    pc_c = vfio_region_read(&vdev->bars[5].region, 0x204, 4);

    return (!(clk & 1) && (0x20100 <= pc_c));
}

static void vfio_radeon_set_gfx_only_reset(VFIOPCIDevice *vdev)
{
    uint32_t misc, fuse;
    bool a, b;

    vfio_region_write(&vdev->bars[5].region, 0x200, 0xc00c0000, 4);
    fuse = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    b = fuse & 64;

    vfio_region_write(&vdev->bars[5].region, 0x200, 0xc0000010, 4);
    misc = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    a = misc & 2;

    if (a == b) {
        vfio_region_write(&vdev->bars[5].region, 0x204, misc ^ 2, 4);
        vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    }
}

static int vfio_radeon_reset(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int i, ret = 0;
    uint32_t data;

    if (vdev->vbasedev.reset_works) {
        trace_vfio_quirk_ati_bonaire_reset_skipped(vdev->vbasedev.name);
        return -ENODEV;
    }

    vfio_pci_write_config(pdev, PCI_COMMAND, PCI_COMMAND_MEMORY, 2);

    if (!vfio_radeon_smc_is_running(vdev)) {
        ret = -EINVAL;
        trace_vfio_quirk_ati_bonaire_reset_no_smc(vdev->vbasedev.name);
        goto out;
    }

    vfio_radeon_set_gfx_only_reset(vdev);

    vfio_pci_write_config(pdev, 0x7c, 0x39d5e86b, 4);
    usleep(100);

    for (i = 0; i < 100000; i++) {
        if (vfio_region_read(&vdev->bars[5].region, 0x5428, 4) != 0xffffffff) {
            goto reset_smc;
        }
        usleep(1);
    }

    trace_vfio_quirk_ati_bonaire_reset_timeout(vdev->vbasedev.name);

reset_smc:
    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000000, 4);
    data = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    data |= 1;
    vfio_region_write(&vdev->bars[5].region, 0x204, data, 4);

    vfio_region_write(&vdev->bars[5].region, 0x200, 0x80000004, 4);
    data = vfio_region_read(&vdev->bars[5].region, 0x204, 4);
    data |= 1;
    vfio_region_write(&vdev->bars[5].region, 0x204, data, 4);

    trace_vfio_quirk_ati_bonaire_reset_done(vdev->vbasedev.name);

out:
    vfio_pci_write_config(pdev, PCI_COMMAND, 0, 2);

    return ret;
}

void vfio_setup_resetfn_quirk(VFIOPCIDevice *vdev)
{
    switch (vdev->vendor_id) {
    case 0x1002:
        switch (vdev->device_id) {
        case 0x6649:
        case 0x6650:
        case 0x6651:
        case 0x6658:
        case 0x665c:
        case 0x665d:
        case 0x67A0:
        case 0x67A1:
        case 0x67A2:
        case 0x67A8:
        case 0x67A9:
        case 0x67AA:
        case 0x67B0:
        case 0x67B1:
        case 0x67B8:
        case 0x67B9:
        case 0x67BA:
        case 0x67BE:
            vdev->resetfn = vfio_radeon_reset;
            trace_vfio_quirk_ati_bonaire_reset(vdev->vbasedev.name);
            break;
        }
        break;
    }
}

static void get_nv_gpudirect_clique_id(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    const Property *prop = opaque;
    uint8_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint8(v, name, ptr, errp);
}

static void set_nv_gpudirect_clique_id(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    const Property *prop = opaque;
    uint8_t value, *ptr = object_field_prop_ptr(obj, prop);

    if (!visit_type_uint8(v, name, &value, errp)) {
        return;
    }

    if (value & ~0xF) {
        error_setg(errp, "Property %s: valid range 0-15", name);
        return;
    }

    *ptr = value;
}

const PropertyInfo qdev_prop_nv_gpudirect_clique = {
    .type = "uint8",
    .description = "NVIDIA GPUDirect Clique ID (0 - 15)",
    .get = get_nv_gpudirect_clique_id,
    .set = set_nv_gpudirect_clique_id,
};

static bool is_valid_std_cap_offset(uint8_t pos)
{
    return (pos >= PCI_STD_HEADER_SIZEOF &&
            pos <= (PCI_CFG_SPACE_SIZE - PCI_CAP_SIZEOF));
}

static bool vfio_add_nv_gpudirect_cap(VFIOPCIDevice *vdev, Error **errp)
{
    ERRP_GUARD();
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int ret, pos;
    bool c8_conflict = false, d4_conflict = false;
    uint8_t tmp;

    if (vdev->nv_gpudirect_clique == 0xFF) {
        return true;
    }

    if (!vfio_pci_is(vdev, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID)) {
        error_setg(errp, "NVIDIA GPUDirect Clique ID: invalid device vendor");
        return false;
    }

    if (pci_get_byte(pdev->config + PCI_CLASS_DEVICE + 1) !=
        PCI_BASE_CLASS_DISPLAY) {
        error_setg(errp, "NVIDIA GPUDirect Clique ID: unsupported PCI class");
        return false;
    }

    ret = pread(vdev->vbasedev.fd, &tmp, 1,
                vdev->config_offset + PCI_CAPABILITY_LIST);
    if (ret != 1 || !is_valid_std_cap_offset(tmp)) {
        error_setg(errp, "NVIDIA GPUDirect Clique ID: error getting cap list");
        return false;
    }

    do {
        if (tmp == 0xC8) {
            c8_conflict = true;
        } else if (tmp == 0xD4) {
            d4_conflict = true;
        }
        tmp = pdev->config[tmp + PCI_CAP_LIST_NEXT];
    } while (is_valid_std_cap_offset(tmp));

    if (!c8_conflict) {
        pos = 0xC8;
    } else if (!d4_conflict) {
        pos = 0xD4;
    } else {
        error_setg(errp, "NVIDIA GPUDirect Clique ID: invalid config space");
        return false;
    }

    ret = pci_add_capability(pdev, PCI_CAP_ID_VNDR, pos, 8, errp);
    if (ret < 0) {
        error_prepend(errp, "Failed to add NVIDIA GPUDirect cap: ");
        return false;
    }

    memset(vdev->emulated_config_bits + pos, 0xFF, 8);
    pos += PCI_CAP_FLAGS;
    pci_set_byte(pdev->config + pos++, 8);
    pci_set_byte(pdev->config + pos++, 'P');
    pci_set_byte(pdev->config + pos++, '2');
    pci_set_byte(pdev->config + pos++, 'P');
    pci_set_byte(pdev->config + pos++, vdev->nv_gpudirect_clique << 3);
    pci_set_byte(pdev->config + pos, 0);

    return true;
}

#define VMD_SHADOW_CAP_VER 1
#define VMD_SHADOW_CAP_LEN 24
static bool vfio_add_vmd_shadow_cap(VFIOPCIDevice *vdev, Error **errp)
{
    ERRP_GUARD();
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint8_t membar_phys[16];
    int ret, pos = 0xE8;

    if (!(vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, 0x201D) ||
          vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, 0x467F) ||
          vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, 0x4C3D) ||
          vfio_pci_is(vdev, PCI_VENDOR_ID_INTEL, 0x9A0B))) {
        return true;
    }

    ret = pread(vdev->vbasedev.fd, membar_phys, 16,
                vdev->config_offset + PCI_BASE_ADDRESS_2);
    if (ret != 16) {
        error_report("VMD %s cannot read MEMBARs (%d)",
                     vdev->vbasedev.name, ret);
        return false;
    }

    ret = pci_add_capability(pdev, PCI_CAP_ID_VNDR, pos,
                             VMD_SHADOW_CAP_LEN, errp);
    if (ret < 0) {
        error_prepend(errp, "Failed to add VMD MEMBAR Shadow cap: ");
        return false;
    }

    memset(vdev->emulated_config_bits + pos, 0xFF, VMD_SHADOW_CAP_LEN);
    pos += PCI_CAP_FLAGS;
    pci_set_byte(pdev->config + pos++, VMD_SHADOW_CAP_LEN);
    pci_set_byte(pdev->config + pos++, VMD_SHADOW_CAP_VER);
    pci_set_long(pdev->config + pos, 0x53484457);
    memcpy(pdev->config + pos + 4, membar_phys, 16);

    return true;
}

bool vfio_add_virt_caps(VFIOPCIDevice *vdev, Error **errp)
{
    if (!vfio_add_nv_gpudirect_cap(vdev, errp)) {
        return false;
    }

    if (!vfio_add_vmd_shadow_cap(vdev, errp)) {
        return false;
    }

    return true;
}
