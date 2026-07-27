/*
 * pcie_aer.h
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_PCIE_AER_H
#define QEMU_PCIE_AER_H

#include "hw/pci/pci_regs.h"

typedef struct PCIEAERErr {
    uint32_t status;
    uint16_t source_id;

#define PCIE_AER_ERR_IS_CORRECTABLE     0x1
#define PCIE_AER_ERR_MAYBE_ADVISORY     0x2
#define PCIE_AER_ERR_HEADER_VALID       0x4
#define PCIE_AER_ERR_TLP_PREFIX_PRESENT 0x8
    uint16_t flags;

    uint32_t header[4];
    uint32_t prefix[4];
} PCIEAERErr;

typedef struct PCIEAERLog {

    uint16_t log_num;

#define PCIE_AER_LOG_MAX_DEFAULT        8
#define PCIE_AER_LOG_MAX_LIMIT          128
    uint16_t log_max;

    PCIEAERErr *log;
} PCIEAERLog;

typedef struct PCIEAERMsg {
    uint32_t severity;

    uint16_t source_id;
} PCIEAERMsg;

static inline bool
pcie_aer_msg_is_uncor(const PCIEAERMsg *msg)
{
    return msg->severity == PCI_ERR_ROOT_CMD_NONFATAL_EN ||
        msg->severity == PCI_ERR_ROOT_CMD_FATAL_EN;
}

extern const VMStateDescription vmstate_pcie_aer_log;

int pcie_aer_init(PCIDevice *dev, uint8_t cap_ver, uint16_t offset,
                  uint16_t size, Error **errp);
void pcie_aer_exit(PCIDevice *dev);
void pcie_aer_write_config(PCIDevice *dev,
                           uint32_t addr, uint32_t val, int len);

void pcie_aer_root_set_vector(PCIDevice *dev, unsigned int vector);
void pcie_aer_root_init(PCIDevice *dev);
void pcie_aer_root_reset(PCIDevice *dev);
void pcie_aer_root_write_config(PCIDevice *dev,
                                uint32_t addr, uint32_t val, int len,
                                uint32_t root_cmd_prev);

int pcie_aer_inject_error(PCIDevice *dev, const PCIEAERErr *err);
#endif
