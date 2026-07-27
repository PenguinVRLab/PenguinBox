/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PPC_SPAPR_FADUMP_H
#define PPC_SPAPR_FADUMP_H

#include "qemu/osdep.h"
#include "cpu.h"

#define FADUMP_CMD_REGISTER            1
#define FADUMP_CMD_UNREGISTER          2
#define FADUMP_CMD_INVALIDATE          3

#define FADUMP_VERSION                 1

#define FADUMP_CPU_STATE_DATA   0x0001
#define FADUMP_HPTE_REGION      0x0002
#define FADUMP_REAL_MODE_REGION 0x0011

#define FADUMP_PARAM_AREA       0x0100

#define FADUMP_REQUEST_FLAG     0x00000001

#define FADUMP_STATUS_DUMP_PERFORMED            0x8000
#define FADUMP_STATUS_DUMP_TRIGGERED            0x4000
#define FADUMP_STATUS_DUMP_ERROR                0x2000

#define FADUMP_ERROR_INVALID_DATA_TYPE          0x8000
#define FADUMP_ERROR_INVALID_SOURCE_ADDR        0x4000
#define FADUMP_ERROR_LENGTH_EXCEEDS_SOURCE      0x2000
#define FADUMP_ERROR_INVALID_DEST_ADDR          0x1000
#define FAUDMP_ERROR_DEST_TOO_SMALL             0x0800

#define FADUMP_MAX_SECTIONS            10

#define FADUMP_PER_CPU_REG_ENTRIES (32 + 45 + 2 )

#define FADUMP_CPU_ID_MASK      ((1ULL << 32) - 1)

typedef struct FadumpSection FadumpSection;
typedef struct FadumpSectionHeader FadumpSectionHeader;
typedef struct FadumpMemStruct FadumpMemStruct;
typedef struct FadumpRegSaveAreaHeader FadumpRegSaveAreaHeader;
typedef struct FadumpRegEntry FadumpRegEntry;

struct SpaprMachineState;

struct FadumpSection {
    uint32_t    request_flag;
    uint16_t    source_data_type;
    uint16_t    error_flags;
    uint64_t    source_address;
    uint64_t    source_len;
    uint64_t    bytes_dumped;
    uint64_t    destination_address;
};

struct FadumpSectionHeader {
    uint32_t    dump_format_version;
    uint16_t    dump_num_sections;
    uint16_t    dump_status_flag;
    uint32_t    offset_first_dump_section;

    uint32_t    dd_block_size;
    uint64_t    dd_block_offset;
    uint64_t    dd_num_blocks;
    uint32_t    dd_offset_disk_path;

    uint32_t    max_time_auto;
};

struct FadumpMemStruct {
    FadumpSectionHeader header;
    FadumpSection       rgn[FADUMP_MAX_SECTIONS];
};

struct FadumpRegSaveAreaHeader {
    uint64_t    magic_number;
    uint32_t    version;
    uint32_t    num_cpu_offset;
};

struct FadumpRegEntry {
    uint64_t    reg_id;
    uint64_t    reg_value;
};

uint32_t do_fadump_register(struct SpaprMachineState *, target_ulong);
void     trigger_fadump_boot(struct SpaprMachineState *, target_ulong);
#endif
