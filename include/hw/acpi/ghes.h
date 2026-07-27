/*
 * Support for generating APEI tables and recording CPER for Guests
 *
 * Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Author: Dongjiu Geng <gengdongjiu@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ACPI_GHES_H
#define ACPI_GHES_H

#include "hw/acpi/bios-linker-loader.h"
#include "qapi/error.h"
#include "qemu/notify.h"

extern NotifierList acpi_generic_error_notifiers;

enum AcpiGhesNotifyType {
    ACPI_GHES_NOTIFY_POLLED = 0,
    ACPI_GHES_NOTIFY_EXTERNAL = 1,
    ACPI_GHES_NOTIFY_LOCAL = 2,
    ACPI_GHES_NOTIFY_SCI = 3,
    ACPI_GHES_NOTIFY_NMI = 4,
    ACPI_GHES_NOTIFY_CMCI = 5,
    ACPI_GHES_NOTIFY_MCE = 6,
    ACPI_GHES_NOTIFY_GPIO = 7,
    ACPI_GHES_NOTIFY_SEA = 8,
    ACPI_GHES_NOTIFY_SEI = 9,
    ACPI_GHES_NOTIFY_GSIV = 10,
    ACPI_GHES_NOTIFY_SDEI = 11,
    ACPI_GHES_NOTIFY_RESERVED = 12
};

enum AcpiGhesSourceID {
    ACPI_HEST_SRC_ID_SYNC,
    ACPI_HEST_SRC_ID_QMP,
};

typedef struct AcpiNotificationSourceId {
    enum AcpiGhesSourceID source_id;
    enum AcpiGhesNotifyType notify;
} AcpiNotificationSourceId;

typedef struct AcpiGhesState {
    uint64_t hest_addr_le;
    uint64_t hw_error_le;
    bool use_hest_addr;
} AcpiGhesState;

void acpi_build_hest(AcpiGhesState *ags, GArray *table_data,
                     GArray *hardware_errors,
                     BIOSLinker *linker,
                     const AcpiNotificationSourceId * const notif_source,
                     int num_sources,
                     const char *oem_id, const char *oem_table_id);
void acpi_ghes_add_fw_cfg(AcpiGhesState *vms, FWCfgState *s,
                          GArray *hardware_errors);
int acpi_ghes_memory_errors(AcpiGhesState *ags, uint16_t source_id,
                            uint64_t error_physical_addr);
void ghes_record_cper_errors(AcpiGhesState *ags, const void *cper, size_t len,
                             uint16_t source_id, Error **errp);

AcpiGhesState *acpi_ghes_get_state(void);
#endif
