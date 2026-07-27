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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/acpi/ghes.h"
#include "hw/acpi/aml-build.h"
#include "qemu/error-report.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/nvram/fw_cfg.h"
#include "qemu/uuid.h"

#define ACPI_HW_ERROR_FW_CFG_FILE           "etc/hardware_errors"
#define ACPI_HW_ERROR_ADDR_FW_CFG_FILE      "etc/hardware_errors_addr"
#define ACPI_HEST_ADDR_FW_CFG_FILE          "etc/acpi_table_hest_addr"

#define ACPI_GHES_MAX_RAW_DATA_LENGTH   (1 * KiB)

#define ACPI_GHES_SOURCE_GENERIC_ERROR_V2   10

#define GAS_ADDR_OFFSET 4

#define ACPI_DESC_HEADER_OFFSET     36

#define ACPI_GHES_DATA_LENGTH               72

#define ACPI_GHES_MEM_CPER_LENGTH           80

#define ACPI_GEBS_UNCORRECTABLE         1

#define ACPI_GHES_GESB_SIZE                 20

#define HEST_GHES_V2_ENTRY_SIZE  92

#define GHES_READ_ACK_ADDR_OFF          64

#define GHES_ERR_STATUS_ADDR_OFF  20

enum AcpiGenericErrorSeverity {
    ACPI_CPER_SEV_RECOVERABLE = 0,
    ACPI_CPER_SEV_FATAL = 1,
    ACPI_CPER_SEV_CORRECTED = 2,
    ACPI_CPER_SEV_NONE = 3,
};

static void build_ghes_hw_error_notification(GArray *table, const uint8_t type)
{
    build_append_int_noprefix(table, type, 1);
    build_append_int_noprefix(table, 28, 1);
    build_append_int_noprefix(table, 0, 2);
    build_append_int_noprefix(table, 0, 4);
    build_append_int_noprefix(table, 0, 4);
    build_append_int_noprefix(table, 0, 4);
    build_append_int_noprefix(table, 0, 4);
    build_append_int_noprefix(table, 0, 4);
    build_append_int_noprefix(table, 0, 4);
}

static void acpi_ghes_generic_error_data(GArray *table,
                const uint8_t *section_type, uint32_t error_severity,
                uint8_t validation_bits, uint8_t flags,
                uint32_t error_data_length, QemuUUID fru_id,
                uint64_t time_stamp)
{
    const uint8_t fru_text[20] = {0};

    g_array_append_vals(table, section_type, 16);

    build_append_int_noprefix(table, error_severity, 4);
    build_append_int_noprefix(table, 0x300, 2);
    build_append_int_noprefix(table, validation_bits, 1);
    build_append_int_noprefix(table, flags, 1);
    build_append_int_noprefix(table, error_data_length, 4);

    g_array_append_vals(table, fru_id.data, ARRAY_SIZE(fru_id.data));

    g_array_append_vals(table, fru_text, sizeof(fru_text));

    build_append_int_noprefix(table, time_stamp, 8);
}

static void acpi_ghes_generic_error_status(GArray *table, uint32_t block_status,
                uint32_t raw_data_offset, uint32_t raw_data_length,
                uint32_t data_length, uint32_t error_severity)
{
    build_append_int_noprefix(table, block_status, 4);
    build_append_int_noprefix(table, raw_data_offset, 4);
    build_append_int_noprefix(table, raw_data_length, 4);
    build_append_int_noprefix(table, data_length, 4);
    build_append_int_noprefix(table, error_severity, 4);
}

static void acpi_ghes_build_append_mem_cper(GArray *table,
                                            uint64_t error_physical_addr)
{

    build_append_int_noprefix(table,
                              (1ULL << 14) |
                              (1ULL << 1) ,
                              8);
    build_append_int_noprefix(table, 0, 8);
    build_append_int_noprefix(table, error_physical_addr, 8);
    build_append_int_noprefix(table, 0, 48);
    build_append_int_noprefix(table, 0 , 1);
    build_append_int_noprefix(table, 0, 7);
}

static void
ghes_gen_err_data_uncorrectable_recoverable(GArray *block,
                                            const uint8_t *section_type,
                                            int data_length)
{
    QemuUUID fru_id = {};

    acpi_ghes_generic_error_status(block, ACPI_GEBS_UNCORRECTABLE,
        0, 0, data_length, ACPI_CPER_SEV_RECOVERABLE);

    acpi_ghes_generic_error_data(block, section_type,
        ACPI_CPER_SEV_RECOVERABLE, 0, 0,
        ACPI_GHES_MEM_CPER_LENGTH, fru_id, 0);
}

static void build_ghes_error_table(AcpiGhesState *ags, GArray *hardware_errors,
                                   BIOSLinker *linker, int num_sources)
{
    int i, error_status_block_offset;

    for (i = 0; i < num_sources; i++) {
        build_append_int_noprefix(hardware_errors, 0, sizeof(uint64_t));
    }

    for (i = 0; i < num_sources; i++) {
        build_append_int_noprefix(hardware_errors, 1, sizeof(uint64_t));
    }

    error_status_block_offset = hardware_errors->len;

    acpi_data_push(hardware_errors,
        ACPI_GHES_MAX_RAW_DATA_LENGTH * num_sources);

    bios_linker_loader_alloc(linker, ACPI_HW_ERROR_FW_CFG_FILE,
                             hardware_errors, sizeof(uint64_t), false);

    for (i = 0; i < num_sources; i++) {
        bios_linker_loader_add_pointer(linker,
                                       ACPI_HW_ERROR_FW_CFG_FILE,
                                       sizeof(uint64_t) * i,
                                       sizeof(uint64_t),
                                       ACPI_HW_ERROR_FW_CFG_FILE,
                                       error_status_block_offset +
                                       i * ACPI_GHES_MAX_RAW_DATA_LENGTH);
    }

    if (!ags->use_hest_addr) {
        bios_linker_loader_write_pointer(linker, ACPI_HW_ERROR_ADDR_FW_CFG_FILE,
                                         0, sizeof(uint64_t),
                                         ACPI_HW_ERROR_FW_CFG_FILE, 0);
    }
}

static void build_ghes_v2_entry(GArray *table_data,
                                BIOSLinker *linker,
                                const AcpiNotificationSourceId *notif_src,
                                uint16_t index, int num_sources)
{
    uint64_t address_offset;
    const uint16_t notify = notif_src->notify;
    const uint16_t source_id = notif_src->source_id;

    build_append_int_noprefix(table_data, ACPI_GHES_SOURCE_GENERIC_ERROR_V2, 2);
    build_append_int_noprefix(table_data, source_id, 2);
    build_append_int_noprefix(table_data, 0xffff, 2);
    build_append_int_noprefix(table_data, 0, 1);
    build_append_int_noprefix(table_data, 1, 1);

    build_append_int_noprefix(table_data, 1, 4);
    build_append_int_noprefix(table_data, 1, 4);
    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    address_offset = table_data->len;
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
                     4 , 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
                                   address_offset + GAS_ADDR_OFFSET,
                                   sizeof(uint64_t),
                                   ACPI_HW_ERROR_FW_CFG_FILE,
                                   index * sizeof(uint64_t));

    build_ghes_hw_error_notification(table_data, notify);

    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    address_offset = table_data->len;
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
                     4 , 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
                                   address_offset + GAS_ADDR_OFFSET,
                                   sizeof(uint64_t),
                                   ACPI_HW_ERROR_FW_CFG_FILE,
                                   (num_sources + index) * sizeof(uint64_t));

    build_append_int_noprefix(table_data, ~0x1ULL, 8);
    build_append_int_noprefix(table_data, 0x1, 8);
}

void acpi_build_hest(AcpiGhesState *ags, GArray *table_data,
                     GArray *hardware_errors,
                     BIOSLinker *linker,
                     const AcpiNotificationSourceId *notif_source,
                     int num_sources,
                     const char *oem_id, const char *oem_table_id)
{
    AcpiTable table = { .sig = "HEST", .rev = 1,
                        .oem_id = oem_id, .oem_table_id = oem_table_id };
    uint32_t hest_offset;
    int i;

    hest_offset = table_data->len;

    build_ghes_error_table(ags, hardware_errors, linker, num_sources);

    acpi_table_begin(&table, table_data);

    build_append_int_noprefix(table_data, num_sources, 4);
    for (i = 0; i < num_sources; i++) {
        build_ghes_v2_entry(table_data, linker, &notif_source[i], i, num_sources);
    }

    acpi_table_end(linker, &table);

    if (ags->use_hest_addr) {
        bios_linker_loader_write_pointer(linker,
                                         ACPI_HEST_ADDR_FW_CFG_FILE, 0,
                                         sizeof(uint64_t),
                                         ACPI_BUILD_TABLE_FILE, hest_offset);
    }
}

void acpi_ghes_add_fw_cfg(AcpiGhesState *ags, FWCfgState *s,
                          GArray *hardware_error)
{
    fw_cfg_add_file(s, ACPI_HW_ERROR_FW_CFG_FILE, hardware_error->data,
                    hardware_error->len);

    if (ags->use_hest_addr) {
        fw_cfg_add_file_callback(s, ACPI_HEST_ADDR_FW_CFG_FILE, NULL, NULL,
            NULL, &(ags->hest_addr_le), sizeof(ags->hest_addr_le), false);
    } else {
        fw_cfg_add_file_callback(s, ACPI_HW_ERROR_ADDR_FW_CFG_FILE, NULL, NULL,
            NULL, &(ags->hw_error_le), sizeof(ags->hw_error_le), false);
    }
}

static void get_hw_error_offsets(uint64_t ghes_addr,
                                 uint64_t *cper_addr,
                                 uint64_t *read_ack_register_addr)
{
    /*
     * non-HEST version supports only one source, so no need to change
     * the start offset based on the source ID. Also, we can't validate
     * the source ID, as it is stored inside the HEST table.
     */

    cpu_physical_memory_read(ghes_addr, cper_addr,
                             sizeof(*cper_addr));

    *cper_addr = le64_to_cpu(*cper_addr);

    *read_ack_register_addr = ghes_addr + sizeof(uint64_t);
}

static void get_ghes_source_offsets(uint16_t source_id,
                                    uint64_t hest_addr,
                                    uint64_t *cper_addr,
                                    uint64_t *read_ack_start_addr,
                                    Error **errp)
{
    uint64_t hest_err_block_addr, hest_read_ack_addr;
    uint64_t err_source_entry, error_block_addr;
    uint32_t num_sources, i;

    hest_addr += ACPI_DESC_HEADER_OFFSET;

    cpu_physical_memory_read(hest_addr, &num_sources,
                             sizeof(num_sources));
    num_sources = le32_to_cpu(num_sources);

    err_source_entry = hest_addr + sizeof(num_sources);

    for (i = 0; i < num_sources; i++) {
        uint64_t addr = err_source_entry;
        uint16_t type, src_id;

        cpu_physical_memory_read(addr, &type, sizeof(type));
        type = le16_to_cpu(type);

        if (type != ACPI_GHES_SOURCE_GENERIC_ERROR_V2) {
            error_setg(errp, "HEST: type %d not supported.", type);
            return;
        }

        addr += sizeof(type);
        cpu_physical_memory_read(addr, &src_id, sizeof(src_id));
        if (le16_to_cpu(src_id) == source_id) {
            break;
        }

        err_source_entry += HEST_GHES_V2_ENTRY_SIZE;
    }
    if (i == num_sources) {
        error_setg(errp, "HEST: Source %d not found.", source_id);
        return;
    }

    hest_err_block_addr = err_source_entry + GHES_ERR_STATUS_ADDR_OFF +
                          GAS_ADDR_OFFSET;

    cpu_physical_memory_read(hest_err_block_addr, &error_block_addr,
                             sizeof(error_block_addr));
    error_block_addr = le64_to_cpu(error_block_addr);

    cpu_physical_memory_read(error_block_addr, cper_addr,
                             sizeof(*cper_addr));
    *cper_addr = le64_to_cpu(*cper_addr);

    hest_read_ack_addr = err_source_entry + GHES_READ_ACK_ADDR_OFF +
                         GAS_ADDR_OFFSET;
    cpu_physical_memory_read(hest_read_ack_addr, read_ack_start_addr,
                             sizeof(*read_ack_start_addr));
    *read_ack_start_addr = le64_to_cpu(*read_ack_start_addr);
}

NotifierList acpi_generic_error_notifiers =
    NOTIFIER_LIST_INITIALIZER(acpi_generic_error_notifiers);

void ghes_record_cper_errors(AcpiGhesState *ags, const void *cper, size_t len,
                             uint16_t source_id, Error **errp)
{
    uint64_t cper_addr = 0, read_ack_register_addr = 0, read_ack_register;

    if (len > ACPI_GHES_MAX_RAW_DATA_LENGTH) {
        error_setg(errp, "GHES CPER record is too big: %zd", len);
        return;
    }

    if (!ags->use_hest_addr) {
        get_hw_error_offsets(le64_to_cpu(ags->hw_error_le),
                             &cper_addr, &read_ack_register_addr);
    } else {
        get_ghes_source_offsets(source_id, le64_to_cpu(ags->hest_addr_le),
                                &cper_addr, &read_ack_register_addr, errp);
    }

    cpu_physical_memory_read(read_ack_register_addr,
                             &read_ack_register, sizeof(read_ack_register));

    if (!read_ack_register) {
        error_setg(errp,
                   "OSPM does not acknowledge previous error,"
                   " so can not record CPER for current error anymore");
        return;
    }

    read_ack_register = cpu_to_le64(0);
    cpu_physical_memory_write(read_ack_register_addr,
                              &read_ack_register, sizeof(uint64_t));

    cpu_physical_memory_write(cper_addr, cper, len);

    notifier_list_notify(&acpi_generic_error_notifiers, &source_id);
}

int acpi_ghes_memory_errors(AcpiGhesState *ags, uint16_t source_id,
                            uint64_t physical_address)
{
    const uint8_t guid[] =
          UUID_LE(0xA5BC1114, 0x6F64, 0x4EDE, 0xB8, 0x63, 0x3E, 0x83, \
                  0xED, 0x7C, 0x83, 0xB1);
    Error *errp = NULL;
    int data_length;
    GArray *block;

    block = g_array_new(false, true , 1);

    data_length = ACPI_GHES_DATA_LENGTH + ACPI_GHES_MEM_CPER_LENGTH;
    assert((data_length + ACPI_GHES_GESB_SIZE) <=
            ACPI_GHES_MAX_RAW_DATA_LENGTH);

    ghes_gen_err_data_uncorrectable_recoverable(block, guid, data_length);

    acpi_ghes_build_append_mem_cper(block, physical_address);

    ghes_record_cper_errors(ags, block->data, block->len, source_id, &errp);

    g_array_free(block, true);

    if (errp) {
        error_report_err(errp);
        return -1;
    }

    return 0;
}

AcpiGhesState *acpi_ghes_get_state(void)
{
    AcpiGedState *acpi_ged_state;
    AcpiGhesState *ags;

    acpi_ged_state = ACPI_GED(object_resolve_path_type("", TYPE_ACPI_GED,
                                                       NULL));

    if (!acpi_ged_state) {
        return NULL;
    }
    ags = &acpi_ged_state->ghes_state;

    if (!ags->hw_error_le && !ags->hest_addr_le) {
        return NULL;
    }
    return ags;
}
