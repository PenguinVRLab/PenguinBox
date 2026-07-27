/*
 * VFIO BASE CONTAINER
 *
 * Copyright (C) 2023 Intel Corporation.
 * Copyright Red Hat, Inc. 2023
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CONTAINER_H
#define HW_VFIO_VFIO_CONTAINER_H

#include "system/memory.h"

typedef struct VFIODevice VFIODevice;
typedef struct VFIOIOMMUClass VFIOIOMMUClass;

typedef struct {
    unsigned long *bitmap;
    hwaddr size;
    hwaddr pages;
} VFIOBitmap;

typedef struct VFIOAddressSpace {
    AddressSpace *as;
    QLIST_HEAD(, VFIOContainer) containers;
    QLIST_ENTRY(VFIOAddressSpace) list;
} VFIOAddressSpace;

struct VFIOContainer {
    Object parent_obj;

    VFIOAddressSpace *space;
    MemoryListener listener;
    Error *error;
    bool initialized;
    uint64_t dirty_pgsizes;
    uint64_t max_dirty_bitmap_size;
    unsigned long pgsizes;
    unsigned int dma_max_mappings;
    bool dirty_pages_supported;
    bool dirty_pages_started;
    QLIST_HEAD(, VFIOGuestIOMMU) giommu_list;
    QLIST_HEAD(, VFIORamDiscardListener) vrdl_list;
    QLIST_ENTRY(VFIOContainer) next;
    QLIST_HEAD(, VFIODevice) device_list;
    GList *iova_ranges;
    NotifierWithReturn cpr_reboot_notifier;
};

#define TYPE_VFIO_IOMMU "vfio-iommu"
OBJECT_DECLARE_TYPE(VFIOContainer, VFIOIOMMUClass, VFIO_IOMMU)

typedef struct VFIOGuestIOMMU {
    VFIOContainer *bcontainer;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(VFIOGuestIOMMU) giommu_next;
} VFIOGuestIOMMU;

typedef struct VFIORamDiscardListener {
    VFIOContainer *bcontainer;
    MemoryRegion *mr;
    hwaddr offset_within_address_space;
    hwaddr size;
    uint64_t granularity;
    RamDiscardListener listener;
    QLIST_ENTRY(VFIORamDiscardListener) next;
} VFIORamDiscardListener;

VFIOAddressSpace *vfio_address_space_get(AddressSpace *as);
void vfio_address_space_put(VFIOAddressSpace *space);
void vfio_address_space_insert(VFIOAddressSpace *space,
                               VFIOContainer *bcontainer);

int vfio_container_dma_map(VFIOContainer *bcontainer,
                           hwaddr iova, uint64_t size,
                           void *vaddr, bool readonly, MemoryRegion *mr);
int vfio_container_dma_unmap(VFIOContainer *bcontainer,
                             hwaddr iova, uint64_t size,
                             IOMMUTLBEntry *iotlb, bool unmap_all);
bool vfio_container_add_section_window(VFIOContainer *bcontainer,
                                       MemoryRegionSection *section,
                                       Error **errp);
void vfio_container_del_section_window(VFIOContainer *bcontainer,
                                       MemoryRegionSection *section);
int vfio_container_set_dirty_page_tracking(VFIOContainer *bcontainer,
                                           bool start, Error **errp);
bool vfio_container_dirty_tracking_is_started(
    const VFIOContainer *bcontainer);
bool vfio_container_devices_dirty_tracking_is_supported(
    const VFIOContainer *bcontainer);
int vfio_container_query_dirty_bitmap(const VFIOContainer *bcontainer,
                                      uint64_t iova, uint64_t size,
                                      hwaddr translated_addr, Error **errp);

GList *vfio_container_get_iova_ranges(const VFIOContainer *bcontainer);

static inline uint64_t
vfio_container_get_page_size_mask(const VFIOContainer *bcontainer)
{
    assert(bcontainer);
    return bcontainer->pgsizes;
}

#define TYPE_VFIO_IOMMU_LEGACY TYPE_VFIO_IOMMU "-legacy"
#define TYPE_VFIO_IOMMU_SPAPR TYPE_VFIO_IOMMU "-spapr"
#define TYPE_VFIO_IOMMU_IOMMUFD TYPE_VFIO_IOMMU "-iommufd"
#define TYPE_VFIO_IOMMU_USER TYPE_VFIO_IOMMU "-user"

struct VFIOIOMMUClass {
    ObjectClass parent_class;

    bool (*setup)(VFIOContainer *bcontainer, Error **errp);

    void (*listener_begin)(VFIOContainer *bcontainer);

    void (*listener_commit)(VFIOContainer *bcontainer);

    int (*dma_map)(const VFIOContainer *bcontainer,
                   hwaddr iova, uint64_t size,
                   void *vaddr, bool readonly, MemoryRegion *mr);
    int (*dma_map_file)(const VFIOContainer *bcontainer,
                        hwaddr iova, uint64_t size,
                        int fd, unsigned long start, bool readonly);
    int (*dma_unmap)(const VFIOContainer *bcontainer,
                     hwaddr iova, uint64_t size,
                     IOMMUTLBEntry *iotlb, bool unmap_all);


    bool (*attach_device)(const char *name, VFIODevice *vbasedev,
                          AddressSpace *as, Error **errp);

    void (*detach_device)(VFIODevice *vbasedev);

    int (*set_dirty_page_tracking)(const VFIOContainer *bcontainer,
                                   bool start, Error **errp);
    int (*query_dirty_bitmap)(const VFIOContainer *bcontainer,
                VFIOBitmap *vbmap, hwaddr iova, hwaddr size, Error **errp);
    int (*pci_hot_reset)(VFIODevice *vbasedev, bool single);

    bool (*add_window)(VFIOContainer *bcontainer,
                       MemoryRegionSection *section,
                       Error **errp);
    void (*del_window)(VFIOContainer *bcontainer,
                       MemoryRegionSection *section);
    void (*release)(VFIOContainer *bcontainer);
};

VFIORamDiscardListener *vfio_find_ram_discard_listener(
    VFIOContainer *bcontainer, MemoryRegionSection *section);

void vfio_container_region_add(VFIOContainer *bcontainer,
                               MemoryRegionSection *section, bool cpr_remap);

#endif
