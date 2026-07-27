/*
 * LASI Wrapper for NCR710 SCSI Controller
 *
 * Copyright (c) 2025 Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>
 * This driver was developed during the Google Summer of Code 2025 program.
 * Mentored by Helge Deller <deller@gmx.de>
 *
 * NCR710 SCSI Controller implementation
 * Based on the NCR53C710 Technical Manual Version 3.2, December 2000
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_LASI_NCR710_H
#define HW_LASI_NCR710_H

#include "hw/sysbus.h"
#include "qemu/osdep.h"
#include "exec/memattrs.h"
#include "hw/scsi/scsi.h"
#include "hw/scsi/ncr53c710.h"

#define TYPE_LASI_NCR710 "lasi-ncr710"
OBJECT_DECLARE_SIMPLE_TYPE(LasiNCR710State, LASI_NCR710)

#define LASI_SCSI_RESET         0x000
#define LASI_SCSI_NCR710_BASE   0x100

#define PARISC_DEVICE_ID_OFF    0x00
#define PARISC_DEVICE_CONFIG_OFF 0x04

#define PHASE_MASK              7
#define PHASE_DO                0

#define NCR710_SCNTL1_RST       0x08
#define NCR710_ISTAT_RST        0x40
#define NCR710_ISTAT_ABRT       0x80
#define NCR710_ISTAT_CON        0x08
#define NCR710_DSTAT_DFE        0x80
#define NCR710_CTEST2_DACK      0x01

typedef struct LasiNCR710State {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq lasi_irq;
    uint32_t hw_type;
    uint32_t sversion;
    uint32_t hversion;
    NCR710State ncr710;
} LasiNCR710State;

DeviceState *lasi_ncr710_init(MemoryRegion *addr_space, hwaddr hpa,
                               qemu_irq irq);
void lasi_ncr710_handle_legacy_cmdline(DeviceState *lasi_dev);

#endif
