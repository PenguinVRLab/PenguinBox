/*
 * Block driver for Hyper-V VHDX Images
 *
 * Copyright (c) 2013 Red Hat, Inc.,
 *
 * Authors:
 *  Jeff Cody <jcody@redhat.com>
 *
 *  This is based on the "VHDX Format Specification v1.00", published 8/25/2012
 *  by Microsoft:
 *      https://www.microsoft.com/en-us/download/details.aspx?id=34750
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef BLOCK_VHDX_H
#define BLOCK_VHDX_H
#include "qemu/units.h"

#define DEFAULT_LOG_SIZE 1048576

#define VHDX_HEADER_BLOCK_SIZE      (64 * KiB)

#define VHDX_FILE_ID_OFFSET         0
#define VHDX_HEADER1_OFFSET         (VHDX_HEADER_BLOCK_SIZE * 1)
#define VHDX_HEADER2_OFFSET         (VHDX_HEADER_BLOCK_SIZE * 2)
#define VHDX_REGION_TABLE_OFFSET    (VHDX_HEADER_BLOCK_SIZE * 3)
#define VHDX_REGION_TABLE2_OFFSET   (VHDX_HEADER_BLOCK_SIZE * 4)

#define VHDX_HEADER_SECTION_END     (1 * MiB)

#define VHDX_FILE_SIGNATURE 0x656C696678646876ULL
typedef struct VHDXFileIdentifier {
    uint64_t    signature;
    uint16_t    creator[256];
} VHDXFileIdentifier;


typedef struct QEMU_PACKED MSGUID {
    uint32_t  data1;
    uint16_t  data2;
    uint16_t  data3;
    uint8_t   data4[8];
} MSGUID;

#define guid_eq(a, b) \
    (memcmp(&(a), &(b), sizeof(MSGUID)) == 0)

#define VHDX_HEADER_SIZE (4 * KiB)

#define VHDX_HEADER_SIGNATURE 0x64616568
typedef struct QEMU_PACKED VHDXHeader {
    uint32_t    signature;
    uint32_t    checksum;
    uint64_t    sequence_number;
    MSGUID      file_write_guid;
    MSGUID      data_write_guid;
    MSGUID      log_guid;
    uint16_t    log_version;
    uint16_t    version;
    uint32_t    log_length;
    uint64_t    log_offset;
} VHDXHeader;

#define VHDX_REGION_SIGNATURE  0x69676572
typedef struct QEMU_PACKED VHDXRegionTableHeader {
    uint32_t    signature;
    uint32_t    checksum;
    uint32_t    entry_count;
    uint32_t    reserved;
} VHDXRegionTableHeader;

#define VHDX_REGION_ENTRY_REQUIRED  0x01
typedef struct QEMU_PACKED VHDXRegionTableEntry {
    MSGUID      guid;
    uint64_t    file_offset;
    uint32_t    length;
    uint32_t    data_bits;
} VHDXRegionTableEntry;


#define VHDX_LOG_MIN_SIZE (1 * MiB)
#define VHDX_LOG_SECTOR_SIZE (4 * KiB)
#define VHDX_LOG_HDR_SIZE 64
#define VHDX_LOG_SIGNATURE 0x65676f6c
typedef struct QEMU_PACKED VHDXLogEntryHeader {
    uint32_t    signature;
    uint32_t    checksum;
    uint32_t    entry_length;
    uint32_t    tail;
    uint64_t    sequence_number;
    uint32_t    descriptor_count;
    uint32_t    reserved;
    MSGUID      log_guid;
    uint64_t    flushed_file_offset;
    uint64_t    last_file_offset;
} VHDXLogEntryHeader;

#define VHDX_LOG_DESC_SIZE 32
#define VHDX_LOG_DESC_SIGNATURE 0x63736564
#define VHDX_LOG_ZERO_SIGNATURE 0x6f72657a
typedef struct QEMU_PACKED VHDXLogDescriptor {
    uint32_t    signature;
    union  {
        uint32_t    reserved;
        uint32_t    trailing_bytes;
    };
    union {
        uint64_t    zero_length;
        uint64_t    leading_bytes;
    };
    uint64_t    file_offset;
    uint64_t    sequence_number;
} VHDXLogDescriptor;

#define VHDX_LOG_DATA_SIGNATURE 0x61746164
typedef struct QEMU_PACKED VHDXLogDataSector {
    uint32_t    data_signature;
    uint32_t    sequence_high;
    uint8_t     data[4084];
    uint32_t    sequence_low;
} VHDXLogDataSector;



#define PAYLOAD_BLOCK_NOT_PRESENT       0
#define PAYLOAD_BLOCK_UNDEFINED         1
#define PAYLOAD_BLOCK_ZERO              2
#define PAYLOAD_BLOCK_UNMAPPED          3
#define PAYLOAD_BLOCK_UNMAPPED_v095     5
#define PAYLOAD_BLOCK_FULLY_PRESENT     6
#define PAYLOAD_BLOCK_PARTIALLY_PRESENT 7

#define SB_BLOCK_NOT_PRESENT    0
#define SB_BLOCK_PRESENT        6

#define VHDX_MAX_SECTORS_PER_BLOCK  (1 << 23)

#define VHDX_BAT_STATE_BIT_MASK 0x07
#define VHDX_BAT_FILE_OFF_MASK  0xFFFFFFFFFFF00000ULL
typedef uint64_t VHDXBatEntry;

#define VHDX_METADATA_ENTRY_SIZE 32
#define VHDX_METADATA_MAX_ENTRIES 2047
#define VHDX_METADATA_TABLE_MAX_SIZE \
    (VHDX_METADATA_ENTRY_SIZE * (VHDX_METADATA_MAX_ENTRIES+1))
#define VHDX_METADATA_SIGNATURE 0x617461646174656DULL
typedef struct QEMU_PACKED VHDXMetadataTableHeader {
    uint64_t    signature;
    uint16_t    reserved;
    uint16_t    entry_count;
    uint32_t    reserved2[5];
} VHDXMetadataTableHeader;

#define VHDX_META_FLAGS_IS_USER         0x01
#define VHDX_META_FLAGS_IS_VIRTUAL_DISK 0x02
#define VHDX_META_FLAGS_IS_REQUIRED     0x04
typedef struct QEMU_PACKED VHDXMetadataTableEntry {
    MSGUID      item_id;
    uint32_t    offset;
    uint32_t    length;
    uint32_t    data_bits;
    uint32_t    reserved2;
} VHDXMetadataTableEntry;

#define VHDX_PARAMS_LEAVE_BLOCKS_ALLOCED 0x01
#define VHDX_PARAMS_HAS_PARENT           0x02
#define VHDX_BLOCK_SIZE_MIN             (1   * MiB)
#define VHDX_BLOCK_SIZE_MAX             (256 * MiB)
typedef struct QEMU_PACKED VHDXFileParameters {
    uint32_t    block_size;
    uint32_t data_bits;
} VHDXFileParameters;

#define VHDX_MAX_IMAGE_SIZE  ((uint64_t) 64 * TiB)
typedef struct QEMU_PACKED VHDXVirtualDiskSize {
    uint64_t    virtual_disk_size;
} VHDXVirtualDiskSize;

typedef struct QEMU_PACKED VHDXPage83Data {
    MSGUID      page_83_data;
} VHDXPage83Data;

typedef struct QEMU_PACKED VHDXVirtualDiskLogicalSectorSize {
    uint32_t    logical_sector_size;
} VHDXVirtualDiskLogicalSectorSize;

typedef struct QEMU_PACKED VHDXVirtualDiskPhysicalSectorSize {
    uint32_t    physical_sector_size;
} VHDXVirtualDiskPhysicalSectorSize;

typedef struct QEMU_PACKED VHDXParentLocatorHeader {
    MSGUID      locator_type;
    uint16_t    reserved;
    uint16_t    key_value_count;
} VHDXParentLocatorHeader;

typedef struct QEMU_PACKED VHDXParentLocatorEntry {
    uint32_t    key_offset;
    uint32_t    value_offset;
    uint16_t    key_length;
    uint16_t    value_length;
} VHDXParentLocatorEntry;


typedef struct VHDXMetadataEntries {
    VHDXMetadataTableEntry file_parameters_entry;
    VHDXMetadataTableEntry virtual_disk_size_entry;
    VHDXMetadataTableEntry page83_data_entry;
    VHDXMetadataTableEntry logical_sector_size_entry;
    VHDXMetadataTableEntry phys_sector_size_entry;
    VHDXMetadataTableEntry parent_locator_entry;
    uint16_t present;
} VHDXMetadataEntries;

typedef struct VHDXLogEntries {
    uint64_t offset;
    uint64_t length;
    uint32_t write;
    uint32_t read;
    VHDXLogEntryHeader *hdr;
    void *desc_buffer;
    uint64_t sequence;
    uint32_t tail;
} VHDXLogEntries;

typedef struct VHDXRegionEntry {
    uint64_t start;
    uint64_t end;
    QLIST_ENTRY(VHDXRegionEntry) entries;
} VHDXRegionEntry;

typedef struct BDRVVHDXState {
    CoMutex lock;

    int curr_header;
    VHDXHeader *headers[2];

    VHDXRegionTableHeader rt;
    VHDXRegionTableEntry bat_rt;
    VHDXRegionTableEntry metadata_rt;

    VHDXMetadataTableHeader metadata_hdr;
    VHDXMetadataEntries metadata_entries;

    VHDXFileParameters params;
    uint32_t block_size;
    uint32_t block_size_bits;
    uint32_t sectors_per_block;
    uint32_t sectors_per_block_bits;

    uint64_t virtual_disk_size;
    uint32_t logical_sector_size;
    uint32_t physical_sector_size;

    uint64_t chunk_ratio;
    uint32_t chunk_ratio_bits;
    uint32_t logical_sector_size_bits;

    uint32_t bat_entries;
    VHDXBatEntry *bat;
    uint64_t bat_offset;

    bool first_visible_write;
    MSGUID session_guid;

    VHDXLogEntries log;

    VHDXParentLocatorHeader parent_header;
    VHDXParentLocatorEntry *parent_entries;

    Error *migration_blocker;

    bool log_replayed_on_open;

    QLIST_HEAD(, VHDXRegionEntry) regions;
} BDRVVHDXState;

void vhdx_guid_generate(MSGUID *guid);

int GRAPH_RDLOCK
vhdx_update_headers(BlockDriverState *bs, BDRVVHDXState *s, bool rw,
                    MSGUID *log_guid);

uint32_t vhdx_update_checksum(uint8_t *buf, size_t size, int crc_offset);
uint32_t vhdx_checksum_calc(uint32_t crc, uint8_t *buf, size_t size,
                            int crc_offset);

bool vhdx_checksum_is_valid(uint8_t *buf, size_t size, int crc_offset);

int GRAPH_RDLOCK
vhdx_parse_log(BlockDriverState *bs, BDRVVHDXState *s, bool *flushed,
               Error **errp);

int coroutine_fn GRAPH_RDLOCK
vhdx_log_write_and_flush(BlockDriverState *bs, BDRVVHDXState *s,
                         void *data, uint32_t length, uint64_t offset);

static inline void leguid_to_cpus(MSGUID *guid)
{
    guid->data1 = le32_to_cpu(guid->data1);
    guid->data2 = le16_to_cpu(guid->data2);
    guid->data3 = le16_to_cpu(guid->data3);
}

static inline void cpu_to_leguids(MSGUID *guid)
{
    guid->data1 = cpu_to_le32(guid->data1);
    guid->data2 = cpu_to_le16(guid->data2);
    guid->data3 = cpu_to_le16(guid->data3);
}

void vhdx_header_le_import(VHDXHeader *h);
void vhdx_header_le_export(VHDXHeader *orig_h, VHDXHeader *new_h);
void vhdx_log_desc_le_import(VHDXLogDescriptor *d);
void vhdx_log_desc_le_export(VHDXLogDescriptor *d);
void vhdx_log_data_le_import(VHDXLogDataSector *d);
void vhdx_log_data_le_export(VHDXLogDataSector *d);
void vhdx_log_entry_hdr_le_import(VHDXLogEntryHeader *hdr);
void vhdx_log_entry_hdr_le_export(VHDXLogEntryHeader *hdr);
void vhdx_region_header_le_import(VHDXRegionTableHeader *hdr);
void vhdx_region_header_le_export(VHDXRegionTableHeader *hdr);
void vhdx_region_entry_le_import(VHDXRegionTableEntry *e);
void vhdx_region_entry_le_export(VHDXRegionTableEntry *e);
void vhdx_metadata_header_le_import(VHDXMetadataTableHeader *hdr);
void vhdx_metadata_header_le_export(VHDXMetadataTableHeader *hdr);
void vhdx_metadata_entry_le_import(VHDXMetadataTableEntry *e);
void vhdx_metadata_entry_le_export(VHDXMetadataTableEntry *e);

int GRAPH_RDLOCK
vhdx_user_visible_write(BlockDriverState *bs, BDRVVHDXState *s);

#endif
