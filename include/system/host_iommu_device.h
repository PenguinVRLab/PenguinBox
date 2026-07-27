/*
 * Host IOMMU device abstract declaration
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef HOST_IOMMU_DEVICE_H
#define HOST_IOMMU_DEVICE_H

#include "qom/object.h"
#include "qapi/error.h"
#ifdef CONFIG_LINUX
#include "linux/iommufd.h"

typedef union VendorCaps {
    struct iommu_hw_info_vtd vtd;
    struct iommu_hw_info_arm_smmuv3 smmuv3;
} VendorCaps;

typedef struct HostIOMMUDeviceCaps {
    uint32_t type;
    uint64_t hw_caps;
    VendorCaps vendor_caps;
} HostIOMMUDeviceCaps;
#endif

#define TYPE_HOST_IOMMU_DEVICE "host-iommu-device"
OBJECT_DECLARE_TYPE(HostIOMMUDevice, HostIOMMUDeviceClass, HOST_IOMMU_DEVICE)

struct HostIOMMUDevice {
    Object parent_obj;

    char *name;
    void *agent;
    PCIBus *aliased_bus;
    int aliased_devfn;
#ifdef CONFIG_LINUX
    HostIOMMUDeviceCaps caps;
#endif
};

struct HostIOMMUDeviceClass {
    ObjectClass parent_class;

    bool (*realize)(HostIOMMUDevice *hiod, void *opaque, Error **errp);
    int (*get_cap)(HostIOMMUDevice *hiod, int cap, Error **errp);
    GList* (*get_iova_ranges)(HostIOMMUDevice *hiod);
    uint64_t (*get_page_size_mask)(HostIOMMUDevice *hiod);
};

#define HOST_IOMMU_DEVICE_CAP_IOMMU_TYPE        0
#define HOST_IOMMU_DEVICE_CAP_AW_BITS           1

#define HOST_IOMMU_DEVICE_CAP_AW_BITS_MAX       64
#endif
