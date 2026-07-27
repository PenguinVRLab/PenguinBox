#ifndef HW_HYPERV_DYNMEM_PROTO_H
#define HW_HYPERV_DYNMEM_PROTO_H

/*
 * Hyper-V Dynamic Memory Protocol definitions
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * Based on drivers/hv/hv_balloon.c from Linux kernel:
 * Copyright (c) 2012, Microsoft Corporation.
 *
 * Author: K. Y. Srinivasan <kys@microsoft.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#define DYNMEM_MAKE_VERSION(Major, Minor) ((uint32_t)(((Major) << 16) | (Minor)))
#define DYNMEM_MAJOR_VERSION(Version) ((uint32_t)(Version) >> 16)
#define DYNMEM_MINOR_VERSION(Version) ((uint32_t)(Version) & 0xff)

enum {
    DYNMEM_PROTOCOL_VERSION_1 = DYNMEM_MAKE_VERSION(0, 3),
    DYNMEM_PROTOCOL_VERSION_2 = DYNMEM_MAKE_VERSION(1, 0),
    DYNMEM_PROTOCOL_VERSION_3 = DYNMEM_MAKE_VERSION(2, 0),

    DYNMEM_PROTOCOL_VERSION_WIN7 = DYNMEM_PROTOCOL_VERSION_1,
    DYNMEM_PROTOCOL_VERSION_WIN8 = DYNMEM_PROTOCOL_VERSION_2,
    DYNMEM_PROTOCOL_VERSION_WIN10 = DYNMEM_PROTOCOL_VERSION_3,

    DYNMEM_PROTOCOL_VERSION_CURRENT = DYNMEM_PROTOCOL_VERSION_WIN10
};



enum dm_message_type {
    DM_ERROR = 0,
    DM_VERSION_REQUEST = 1,
    DM_VERSION_RESPONSE = 2,
    DM_CAPABILITIES_REPORT = 3,
    DM_CAPABILITIES_RESPONSE = 4,
    DM_STATUS_REPORT = 5,
    DM_BALLOON_REQUEST = 6,
    DM_BALLOON_RESPONSE = 7,
    DM_UNBALLOON_REQUEST = 8,
    DM_UNBALLOON_RESPONSE = 9,
    DM_MEM_HOT_ADD_REQUEST = 10,
    DM_MEM_HOT_ADD_RESPONSE = 11,
    DM_VERSION_03_MAX = 11,
    DM_INFO_MESSAGE = 12,
    DM_VERSION_1_MAX = 12,

    DM_MEM_HOT_REMOVE_REQUEST = 13,
    DM_MEM_HOT_REMOVE_RESPONSE = 14
};


union dm_version {
    struct {
        uint16_t minor_version;
        uint16_t major_version;
    };
    uint32_t version;
} QEMU_PACKED;


union dm_caps {
    struct {
        uint64_t balloon:1;
        uint64_t hot_add:1;
        uint64_t hot_add_alignment:4;
        uint64_t hot_remove:1;
        uint64_t reservedz:57;
    } cap_bits;
    uint64_t caps;
} QEMU_PACKED;

union dm_mem_page_range {
    struct  {
        uint64_t start_page:40;
        uint64_t page_cnt:24;
    } finfo;
    uint64_t  page_range;
} QEMU_PACKED;



struct dm_header {
    uint16_t type;
    uint16_t size;
    uint32_t trans_id;
} QEMU_PACKED;

struct dm_message {
    struct dm_header hdr;
    uint8_t data[];
} QEMU_PACKED;


struct dm_version_request {
    struct dm_header hdr;
    union dm_version version;
    uint32_t is_last_attempt:1;
    uint32_t reservedz:31;
} QEMU_PACKED;

struct dm_version_response {
    struct dm_header hdr;
    uint64_t is_accepted:1;
    uint64_t reservedz:63;
} QEMU_PACKED;

struct dm_capabilities {
    struct dm_header hdr;
    union dm_caps caps;
    uint64_t min_page_cnt;
    uint64_t max_page_number;
} QEMU_PACKED;

struct dm_capabilities_resp_msg {
    struct dm_header hdr;
    uint64_t is_accepted:1;
    uint64_t hot_remove:1;
    uint64_t suppress_pressure_reports:1;
    uint64_t reservedz:61;
} QEMU_PACKED;

struct dm_status {
    struct dm_header hdr;
    uint64_t num_avail;
    uint64_t num_committed;
    uint64_t page_file_size;
    uint64_t zero_free;
    uint32_t page_file_writes;
    uint32_t io_diff;
} QEMU_PACKED;


struct dm_balloon {
    struct dm_header hdr;
    uint32_t num_pages;
    uint32_t reservedz;
} QEMU_PACKED;


struct dm_balloon_response {
    struct dm_header hdr;
    uint32_t reservedz;
    uint32_t more_pages:1;
    uint32_t range_count:31;
    union dm_mem_page_range range_array[];
} QEMU_PACKED;

struct dm_unballoon_request {
    struct dm_header hdr;
    uint32_t more_pages:1;
    uint32_t reservedz:31;
    uint32_t range_count;
    union dm_mem_page_range range_array[];
} QEMU_PACKED;

struct dm_unballoon_response {
    struct dm_header hdr;
} QEMU_PACKED;


struct dm_hot_add {
    struct dm_header hdr;
    union dm_mem_page_range range;
} QEMU_PACKED;

struct dm_hot_add_with_region {
    struct dm_header hdr;
    union dm_mem_page_range range;
    union dm_mem_page_range region;
} QEMU_PACKED;

struct dm_hot_add_response {
    struct dm_header hdr;
    uint32_t page_count;
    uint32_t result;
} QEMU_PACKED;

struct dm_hot_remove {
    struct dm_header hdr;
    uint32_t virtual_node;
    uint32_t page_count;
    uint32_t qos_flags;
    uint32_t reservedZ;
} QEMU_PACKED;

struct dm_hot_remove_response {
    struct dm_header hdr;
    uint32_t result;
    uint32_t range_count;
    uint64_t more_pages:1;
    uint64_t reservedz:63;
    union dm_mem_page_range range_array[];
} QEMU_PACKED;

#define DM_REMOVE_QOS_LARGE (1 << 0)
#define DM_REMOVE_QOS_LOCAL (1 << 1)
#define DM_REMOVE_QOS_MASK (0x3)

enum dm_info_type {
    INFO_TYPE_MAX_PAGE_CNT = 0,
    MAX_INFO_TYPE
};


struct dm_info_header {
    enum dm_info_type type;
    uint32_t data_size;
    uint8_t  data[];
} QEMU_PACKED;

struct dm_info_msg {
    struct dm_header hdr;
    uint32_t reserved;
    uint32_t info_size;
    uint8_t  info[];
};

#endif
