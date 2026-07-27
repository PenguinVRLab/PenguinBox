/*
 * pcie_sriov.h:
 *
 * Implementation of SR/IOV emulation support.
 *
 * Copyright (c) 2015 Knut Omang <knut.omang@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PCIE_SRIOV_H
#define QEMU_PCIE_SRIOV_H

#include "hw/pci/pci.h"

typedef struct PCIESriovPF {
    uint8_t vf_bar_type[PCI_NUM_REGIONS];
    PCIDevice **vf;
    bool vf_user_created;
} PCIESriovPF;

typedef struct PCIESriovVF {
    PCIDevice *pf;
    uint16_t vf_number;
} PCIESriovVF;

bool pcie_sriov_pf_init(PCIDevice *dev, uint16_t offset,
                        const char *vfname, uint16_t vf_dev_id,
                        uint16_t init_vfs, uint16_t total_vfs,
                        uint16_t vf_offset, uint16_t vf_stride,
                        Error **errp);
void pcie_sriov_pf_exit(PCIDevice *dev);

void pcie_sriov_pf_init_vf_bar(PCIDevice *dev, int region_num,
                               uint8_t type, dma_addr_t size);

int16_t pcie_sriov_pf_init_from_user_created_vfs(PCIDevice *dev,
                                                 uint16_t offset,
                                                 Error **errp);

bool pcie_sriov_register_device(PCIDevice *dev, Error **errp);
void pcie_sriov_unregister_device(PCIDevice *dev);

#define SRIOV_SUP_PGSIZE_MINREQ 0x553

void pcie_sriov_pf_add_sup_pgsize(PCIDevice *dev, uint16_t opt_sup_pgsize);

void pcie_sriov_config_write(PCIDevice *dev, uint32_t address,
                             uint32_t val, int len);

void pcie_sriov_pf_post_load(PCIDevice *dev);

void pcie_sriov_pf_reset(PCIDevice *dev);

uint16_t pcie_sriov_vf_number(PCIDevice *dev);

PCIDevice *pcie_sriov_get_pf(PCIDevice *dev);

PCIDevice *pcie_sriov_get_vf_at_index(PCIDevice *dev, int n);

uint16_t pcie_sriov_num_vfs(PCIDevice *dev);

#endif
