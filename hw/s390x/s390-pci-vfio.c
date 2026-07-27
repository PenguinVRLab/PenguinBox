/*
 * s390 vfio-pci interfaces
 *
 * Copyright 2020 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include <linux/vfio_zdev.h>

#include "trace.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-clp.h"
#include "hw/s390x/s390-pci-vfio.h"
#include "hw/vfio/pci.h"
#include "hw/vfio/vfio-container-legacy.h"
#include "hw/vfio/vfio-helpers.h"

bool s390_pci_update_dma_avail(int fd, unsigned int *avail)
{
    uint32_t argsz = sizeof(struct vfio_iommu_type1_info);
    g_autofree struct vfio_iommu_type1_info *info = g_malloc0(argsz);

    assert(avail);

retry:
    info->argsz = argsz;

    if (ioctl(fd, VFIO_IOMMU_GET_INFO, info)) {
        return false;
    }

    if (info->argsz > argsz) {
        argsz = info->argsz;
        info = g_realloc(info, argsz);
        goto retry;
    }

    return vfio_get_info_dma_avail(info, avail);
}

S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                          S390PCIBusDevice *pbdev)
{
    S390PCIDMACount *cnt;
    uint32_t avail;
    VFIOPCIDevice *vpdev = VFIO_PCI_DEVICE(pbdev->pdev);
    int id;

    assert(vpdev);

    if (!vpdev->vbasedev.group) {
        return NULL;
    }

    id = vpdev->vbasedev.group->container->fd;

    if (!s390_pci_update_dma_avail(id, &avail)) {
        return NULL;
    }

    QTAILQ_FOREACH(cnt, &s->zpci_dma_limit, link) {
        if (cnt->id  == id) {
            cnt->users++;
            return cnt;
        }
    }

    cnt = g_new0(S390PCIDMACount, 1);
    cnt->id = id;
    cnt->users = 1;
    cnt->avail = avail;
    QTAILQ_INSERT_TAIL(&s->zpci_dma_limit, cnt, link);
    pbdev->iommu->max_dma_limit = avail;
    return cnt;
}

void s390_pci_end_dma_count(S390pciState *s, S390PCIDMACount *cnt)
{
    assert(cnt);

    cnt->users--;
    if (cnt->users == 0) {
        QTAILQ_REMOVE(&s->zpci_dma_limit, cnt, link);
    }
}

static void s390_pci_read_base(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_base *cap;
    VFIOPCIDevice *vpci = VFIO_PCI_DEVICE(pbdev->pdev);
    uint64_t vfio_size;

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_BASE);

    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_BASE);
        return;
    }
    cap = (void *) hdr;

    pbdev->zpci_fn.sdma = cap->start_dma;
    pbdev->zpci_fn.edma = cap->end_dma;
    pbdev->zpci_fn.pchid = cap->pchid;
    pbdev->zpci_fn.vfn = cap->vfn;
    pbdev->zpci_fn.pfgid = cap->gid;
    pbdev->zpci_fn.fmbl = 0;
    pbdev->zpci_fn.pft = 0;
    pbdev->pft = cap->pft;

    if (pbdev->pft == ZPCI_PFT_ISM) {
        pbdev->rtr_avail = false;
    }

    if (!pbdev->rtr_avail) {
        vfio_size = pbdev->iommu->max_dma_limit << TARGET_PAGE_BITS;
        if (vfio_size > 0 && vfio_size < cap->end_dma - cap->start_dma + 1) {
            pbdev->zpci_fn.edma = cap->start_dma + vfio_size - 1;
        }
    }
}

static bool get_host_fh(S390PCIBusDevice *pbdev, struct vfio_device_info *info,
                        uint32_t *fh)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_base *cap;
    VFIOPCIDevice *vpci = VFIO_PCI_DEVICE(pbdev->pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_BASE);

    if (hdr == NULL || hdr->version < 2) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_BASE);
        return false;
    }
    cap = (void *) hdr;

    *fh = cap->fh;
    return true;
}

static void s390_pci_read_group(S390PCIBusDevice *pbdev,
                                struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_group *cap;
    S390pciState *s = s390_get_phb();
    ClpRspQueryPciGrp *resgrp;
    VFIOPCIDevice *vpci = VFIO_PCI_DEVICE(pbdev->pdev);
    uint8_t start_gid = pbdev->zpci_fn.pfgid;

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_GROUP);

    if (hdr == NULL || pbdev->zpci_fn.pfgid >= ZPCI_SIM_GRP_START) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_GROUP);
        pbdev->zpci_fn.pfgid = ZPCI_DEFAULT_FN_GRP;
        pbdev->pci_group = s390_group_find(ZPCI_DEFAULT_FN_GRP);
        return;
    }
    cap = (void *) hdr;

    if (!pbdev->interp) {
        pbdev->pci_group = s390_group_find_host_sim(pbdev->zpci_fn.pfgid);
        if (pbdev->pci_group) {
            pbdev->zpci_fn.pfgid = pbdev->pci_group->id;
            return;
        } else {
            if (s->next_sim_grp == ZPCI_DEFAULT_FN_GRP) {
                trace_s390_pci_clp_cap(vpci->vbasedev.name,
                                       VFIO_DEVICE_INFO_CAP_ZPCI_GROUP);
                pbdev->zpci_fn.pfgid = ZPCI_DEFAULT_FN_GRP;
                pbdev->pci_group = s390_group_find(ZPCI_DEFAULT_FN_GRP);
                return;
            } else {
                pbdev->zpci_fn.pfgid = s->next_sim_grp;
                s->next_sim_grp++;
            }
        }
    }

    pbdev->pci_group = s390_group_find(pbdev->zpci_fn.pfgid);

    if (!pbdev->pci_group) {
        pbdev->pci_group = s390_group_create(pbdev->zpci_fn.pfgid, start_gid);

        resgrp = &pbdev->pci_group->zpci_group;
        if (pbdev->rtr_avail) {
            resgrp->fr |= CLP_RSP_QPCIG_MASK_RTR;
        }
        if (cap->flags & VFIO_DEVICE_INFO_ZPCI_FLAG_REFRESH) {
            resgrp->fr |= CLP_RSP_QPCIG_MASK_REFRESH;
        }
        resgrp->dasm = cap->dasm;
        resgrp->msia = cap->msi_addr;
        resgrp->mui = cap->mui;
        resgrp->i = cap->noi;
        if (pbdev->interp && hdr->version >= 2) {
            resgrp->maxstbl = cap->imaxstbl;
        } else {
            resgrp->maxstbl = cap->maxstbl;
        }
        resgrp->version = cap->version;
        resgrp->dtsm = ZPCI_DTSM;
    }
}

static void s390_pci_read_util(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_util *cap;
    VFIOPCIDevice *vpci = VFIO_PCI_DEVICE(pbdev->pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);

    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);
        return;
    }
    cap = (void *) hdr;

    if (cap->size > CLP_UTIL_STR_LEN) {
        trace_s390_pci_clp_cap_size(vpci->vbasedev.name, cap->size,
                                    VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);
        return;
    }

    pbdev->zpci_fn.flags |= CLP_RSP_QPCI_MASK_UTIL;
    memcpy(pbdev->zpci_fn.util_str, cap->util_str, CLP_UTIL_STR_LEN);
}

static void s390_pci_read_pfip(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_pfip *cap;
    VFIOPCIDevice *vpci = VFIO_PCI_DEVICE(pbdev->pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);

    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);
        return;
    }
    cap = (void *) hdr;

    if (cap->size > CLP_PFIP_NR_SEGMENTS) {
        trace_s390_pci_clp_cap_size(vpci->vbasedev.name, cap->size,
                                    VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);
        return;
    }

    memcpy(pbdev->zpci_fn.pfip, cap->pfip, CLP_PFIP_NR_SEGMENTS);
}

static struct vfio_device_info *get_device_info(S390PCIBusDevice *pbdev)
{
    VFIOPCIDevice *vfio_pci = VFIO_PCI_DEVICE(pbdev->pdev);

    return vfio_get_device_info(vfio_pci->vbasedev.fd);
}

bool s390_pci_get_host_fh(S390PCIBusDevice *pbdev, uint32_t *fh)
{
    g_autofree struct vfio_device_info *info = NULL;

    assert(fh);

    info = get_device_info(pbdev);
    if (!info) {
        return false;
    }

    return get_host_fh(pbdev, info, fh);
}

void s390_pci_get_clp_info(S390PCIBusDevice *pbdev)
{
    g_autofree struct vfio_device_info *info = NULL;

    info = get_device_info(pbdev);
    if (!info) {
        return;
    }

    s390_pci_read_base(pbdev, info);
    s390_pci_read_group(pbdev, info);
    s390_pci_read_util(pbdev, info);
    s390_pci_read_pfip(pbdev, info);
}
