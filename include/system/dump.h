/*
 * QEMU dump
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef DUMP_H
#define DUMP_H

#include "qapi/qapi-types-dump.h"
#include "qemu/thread.h"

#define MAKEDUMPFILE_SIGNATURE      "makedumpfile"
#define MAX_SIZE_MDF_HEADER         (4096)
#define TYPE_FLAT_HEADER            (1)
#define VERSION_FLAT_HEADER         (1)
#define END_FLAG_FLAT_HEADER        (-1)

#ifndef ARCH_PFN_OFFSET
#define ARCH_PFN_OFFSET             (0)
#endif

#define DUMP_DH_COMPRESSED_ZLIB     (0x1)
#define DUMP_DH_COMPRESSED_LZO      (0x2)
#define DUMP_DH_COMPRESSED_SNAPPY   (0x4)

#define KDUMP_SIGNATURE             "KDUMP   "
#define SIG_LEN                     (sizeof(KDUMP_SIGNATURE) - 1)
#define DUMP_LEVEL                  (1)
#define DISKDUMP_HEADER_BLOCKS      (1)

#include "system/dump-arch.h"
#include "system/memory_mapping.h"

typedef struct QEMU_PACKED MakedumpfileHeader {
    char signature[16];
    int64_t type;
    int64_t version;
} MakedumpfileHeader;

typedef struct QEMU_PACKED MakedumpfileDataHeader {
    int64_t offset;
    int64_t buf_size;
} MakedumpfileDataHeader;

typedef struct QEMU_PACKED NewUtsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} NewUtsname;

typedef struct QEMU_PACKED DiskDumpHeader32 {
    char signature[SIG_LEN];
    uint32_t header_version;
    NewUtsname utsname;
    char timestamp[10];
    uint32_t status;
    uint32_t block_size;
    uint32_t sub_hdr_size;
    uint32_t bitmap_blocks;
    uint32_t max_mapnr;
    uint32_t total_ram_blocks;
    uint32_t device_blocks;
    uint32_t written_blocks;
    uint32_t current_cpu;
    uint32_t nr_cpus;
} DiskDumpHeader32;

typedef struct QEMU_PACKED DiskDumpHeader64 {
    char signature[SIG_LEN];
    uint32_t header_version;
    NewUtsname utsname;
    char timestamp[22];
    uint32_t status;
    uint32_t block_size;
    uint32_t sub_hdr_size;
    uint32_t bitmap_blocks;
    uint32_t max_mapnr;
    uint32_t total_ram_blocks;
    uint32_t device_blocks;
    uint32_t written_blocks;
    uint32_t current_cpu;
    uint32_t nr_cpus;
} DiskDumpHeader64;

typedef struct QEMU_PACKED KdumpSubHeader32 {
    uint32_t phys_base;
    uint32_t dump_level;
    uint32_t split;
    uint32_t start_pfn;
    uint32_t end_pfn;
    uint64_t offset_vmcoreinfo;
    uint32_t size_vmcoreinfo;
    uint64_t offset_note;
    uint32_t note_size;
    uint64_t offset_eraseinfo;
    uint32_t size_eraseinfo;
    uint64_t start_pfn_64;
    uint64_t end_pfn_64;
    uint64_t max_mapnr_64;
} KdumpSubHeader32;

typedef struct QEMU_PACKED KdumpSubHeader64 {
    uint64_t phys_base;
    uint32_t dump_level;
    uint32_t split;
    uint64_t start_pfn;
    uint64_t end_pfn;
    uint64_t offset_vmcoreinfo;
    uint64_t size_vmcoreinfo;
    uint64_t offset_note;
    uint64_t note_size;
    uint64_t offset_eraseinfo;
    uint64_t size_eraseinfo;
    uint64_t start_pfn_64;
    uint64_t end_pfn_64;
    uint64_t max_mapnr_64;
} KdumpSubHeader64;

typedef struct DataCache {
    DumpState *state;
    uint8_t *buf;
    size_t buf_size;
    size_t data_size;
    off_t offset;
} DataCache;

typedef struct QEMU_PACKED PageDescriptor {
    uint64_t offset;
    uint32_t size;
    uint32_t flags;
    uint64_t page_flags;
} PageDescriptor;

typedef struct DumpState {
    GuestPhysBlockList guest_phys_blocks;
    ArchDumpInfo dump_info;
    MemoryMappingList list;
    bool resume;
    bool detached;
    bool kdump_raw;
    hwaddr memory_offset;
    int fd;

    int64_t filter_area_begin;
    int64_t filter_area_length;

    uint32_t phdr_num;
    uint32_t shdr_num;
    ssize_t note_size;
    hwaddr shdr_offset;
    hwaddr phdr_offset;
    hwaddr section_offset;
    hwaddr note_offset;

    void *elf_section_hdrs;
    void *elf_section_data;
    uint64_t elf_section_data_size;
    GArray *string_table_buf;

    uint8_t *note_buf;
    size_t note_buf_offset;
    uint32_t nr_cpus;
    uint64_t max_mapnr;
    size_t len_dump_bitmap;
    off_t offset_dump_bitmap;
    off_t offset_page;
    size_t num_dumpable;
    uint32_t flag_compress;
    DumpStatus status;

    bool has_format;
    DumpGuestMemoryFormat format;
    QemuThread dump_thread;

    int64_t total_size;
    int64_t written_size;
    uint8_t *guest_note;
    size_t guest_note_size;
} DumpState;

uint16_t cpu_to_dump16(DumpState *s, uint16_t val);
uint32_t cpu_to_dump32(DumpState *s, uint32_t val);
uint64_t cpu_to_dump64(DumpState *s, uint64_t val);

int64_t dump_filtered_memblock_size(GuestPhysBlock *block, int64_t filter_area_start,
                                    int64_t filter_area_length);
int64_t dump_filtered_memblock_start(GuestPhysBlock *block, int64_t filter_area_start,
                                     int64_t filter_area_length);
#endif
