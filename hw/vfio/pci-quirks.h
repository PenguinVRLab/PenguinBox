/*
 * vfio generic region quirks (mostly backdoors to PCI config space)
 *
 * Copyright Red Hat, Inc. 2012-2015
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef HW_VFIO_VFIO_PCI_QUIRKS_H
#define HW_VFIO_VFIO_PCI_QUIRKS_H

#include "exec/memop.h"

typedef struct VFIOConfigWindowMatch {
    uint32_t match;
    uint32_t mask;
} VFIOConfigWindowMatch;

typedef struct VFIOConfigWindowQuirk {
    struct VFIOPCIDevice *vdev;

    uint32_t address_val;

    uint32_t address_offset;
    uint32_t data_offset;

    bool window_enabled;
    uint8_t bar;

    MemoryRegion *addr_mem;
    MemoryRegion *data_mem;

    uint32_t nr_matches;
    VFIOConfigWindowMatch matches[];
} VFIOConfigWindowQuirk;

extern const MemoryRegionOps vfio_generic_window_address_quirk;
extern const MemoryRegionOps vfio_generic_window_data_quirk;

typedef struct VFIOConfigMirrorQuirk {
    struct VFIOPCIDevice *vdev;
    uint32_t offset;
    uint32_t config_offset;
    uint8_t bar;
    MemoryRegion *mem;
    uint8_t data[];
} VFIOConfigMirrorQuirk;

extern const MemoryRegionOps vfio_generic_mirror_quirk;

#endif
