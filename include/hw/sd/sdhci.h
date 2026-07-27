/*
 * SD Association Host Standard Specification v2.0 controller emulation
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Mitsyanko Igor <i.mitsyanko@samsung.com>
 * Peter A.G. Crosthwaite <peter.crosthwaite@petalogix.com>
 *
 * Based on MMC controller for Samsung S5PC1xx-based board emulation
 * by Alexey Merkulov and Vladimir Monakhov.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU _General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SDHCI_H
#define SDHCI_H

#include "hw/pci/pci_device.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "qom/object.h"

struct SDHCIState {
    union {
        PCIDevice pcidev;
        SysBusDevice busdev;
    };

    SDBus sdbus;
    MemoryRegion iomem;
    AddressSpace sysbus_dma_as;
    AddressSpace *dma_as;
    MemoryRegion *dma_mr;
    const MemoryRegionOps *io_ops;

    QEMUTimer *insert_timer;
    QEMUTimer *transfer_timer;
    qemu_irq irq;

    uint32_t sdmasysad;
    uint16_t blksize;
    uint16_t blkcnt;
    uint32_t argument;
    uint16_t trnmod;
    uint16_t cmdreg;
    uint32_t rspreg[4];
    uint32_t prnsts;
    uint8_t  hostctl1;
    uint8_t  pwrcon;
    uint8_t  blkgap;
    uint8_t  wakcon;
    uint16_t clkcon;
    uint8_t  timeoutcon;
    uint8_t  admaerr;
    uint16_t norintsts;
    uint16_t errintsts;
    uint16_t norintstsen;
    uint16_t errintstsen;
    uint16_t norintsigen;
    uint16_t errintsigen;
    uint16_t acmd12errsts;
    uint16_t hostctl2;
    uint64_t admasysaddr;
    uint16_t vendor_spec;

    uint64_t capareg;
    uint64_t maxcurr;
    uint16_t version;

    uint8_t  *fifo_buffer;
    uint32_t buf_maxsz;
    uint16_t data_count;
    uint8_t  stopped_state;
    bool     pending_insert_state;

    bool pending_insert_quirk;
    uint32_t quirks;
    uint8_t endianness;
    uint8_t sd_spec_version;
    uint8_t uhs_mode;
    uint8_t vendor;
    bool wp_inverted;
};
typedef struct SDHCIState SDHCIState;

#define SDHCI_VENDOR_NONE       0
#define SDHCI_VENDOR_FSL        2

#define SDHCI_QUIRK_NO_BUSY_IRQ    BIT(14)

#define TYPE_PCI_SDHCI "sdhci-pci"
DECLARE_INSTANCE_CHECKER(SDHCIState, PCI_SDHCI,
                         TYPE_PCI_SDHCI)

#define TYPE_SYSBUS_SDHCI "generic-sdhci"
DECLARE_INSTANCE_CHECKER(SDHCIState, SYSBUS_SDHCI,
                         TYPE_SYSBUS_SDHCI)

#define TYPE_IMX_USDHC "imx-usdhc"

#define TYPE_S3C_SDHCI "s3c-sdhci"

#endif
