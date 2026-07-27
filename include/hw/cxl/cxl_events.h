/*
 * QEMU CXL Events
 *
 * Copyright (c) 2022 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_EVENTS_H
#define CXL_EVENTS_H

#include "qemu/uuid.h"

typedef enum CXLEventLogType {
    CXL_EVENT_TYPE_INFO          = 0,
    CXL_EVENT_TYPE_WARN          = 1,
    CXL_EVENT_TYPE_FAIL          = 2,
    CXL_EVENT_TYPE_FATAL         = 3,
    CXL_EVENT_TYPE_DYNAMIC_CAP   = 4,
    CXL_EVENT_TYPE_MAX
} CXLEventLogType;

#define CXL_EVENT_REC_HDR_RES_LEN 0xf
typedef struct CXLEventRecordHdr {
    QemuUUID id;
    uint8_t length;
    uint8_t flags[3];
    uint16_t handle;
    uint16_t related_handle;
    uint64_t timestamp;
    uint8_t maint_op_class;
    uint8_t reserved[CXL_EVENT_REC_HDR_RES_LEN];
} QEMU_PACKED CXLEventRecordHdr;

#define CXL_EVENT_RECORD_DATA_LENGTH 0x50
typedef struct CXLEventRecordRaw {
    CXLEventRecordHdr hdr;
    uint8_t data[CXL_EVENT_RECORD_DATA_LENGTH];
} QEMU_PACKED CXLEventRecordRaw;
#define CXL_EVENT_RECORD_SIZE (sizeof(CXLEventRecordRaw))

#define CXL_GET_EVENT_FLAG_OVERFLOW     BIT(0)
#define CXL_GET_EVENT_FLAG_MORE_RECORDS BIT(1)
typedef struct CXLGetEventPayload {
    uint8_t flags;
    uint8_t reserved1;
    uint16_t overflow_err_count;
    uint64_t first_overflow_timestamp;
    uint64_t last_overflow_timestamp;
    uint16_t record_count;
    uint8_t reserved2[0xa];
    CXLEventRecordRaw records[];
} QEMU_PACKED CXLGetEventPayload;
#define CXL_EVENT_PAYLOAD_HDR_SIZE (sizeof(CXLGetEventPayload))

typedef struct CXLClearEventPayload {
    uint8_t event_log;
    uint8_t clear_flags;
    uint8_t nr_recs;
    uint8_t reserved[3];
    uint16_t handle[];
} CXLClearEventPayload;

typedef enum CXLEventIntMode {
    CXL_INT_NONE     = 0x00,
    CXL_INT_MSI_MSIX = 0x01,
    CXL_INT_FW       = 0x02,
    CXL_INT_RES      = 0x03,
} CXLEventIntMode;
#define CXL_EVENT_INT_MODE_MASK 0x3
#define CXL_EVENT_INT_SETTING(vector) \
    ((((uint8_t)vector & 0xf) << 4) | CXL_INT_MSI_MSIX)
typedef struct CXLEventInterruptPolicy {
    uint8_t info_settings;
    uint8_t warn_settings;
    uint8_t failure_settings;
    uint8_t fatal_settings;
    uint8_t dyn_cap_settings;
} QEMU_PACKED CXLEventInterruptPolicy;
#define CXL_EVENT_INT_SETTING_MIN_LEN 4

#define CXL_EVENT_GEN_MED_COMP_ID_SIZE  0x10
#define CXL_EVENT_GEN_MED_RES_SIZE      0x2e
typedef struct CXLEventGenMedia {
    CXLEventRecordHdr hdr;
    uint64_t phys_addr;
    uint8_t descriptor;
    uint8_t type;
    uint8_t transaction_type;
    uint16_t validity_flags;
    uint8_t channel;
    uint8_t rank;
    uint8_t device[3];
    uint8_t component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
    uint8_t reserved[CXL_EVENT_GEN_MED_RES_SIZE];
} QEMU_PACKED CXLEventGenMedia;

typedef struct CXLEventDram {
    CXLEventRecordHdr hdr;
    uint64_t phys_addr;
    uint8_t descriptor;
    uint8_t type;
    uint8_t transaction_type;
    uint16_t validity_flags;
    uint8_t channel;
    uint8_t rank;
    uint8_t nibble_mask[3];
    uint8_t bank_group;
    uint8_t bank;
    uint8_t row[3];
    uint16_t column;
    uint64_t correction_mask[4];
    uint8_t reserved[0x17];
} QEMU_PACKED CXLEventDram;

typedef struct CXLEventMemoryModule {
    CXLEventRecordHdr hdr;
    uint8_t type;
    uint8_t health_status;
    uint8_t media_status;
    uint8_t additional_status;
    uint8_t life_used;
    int16_t temperature;
    uint32_t dirty_shutdown_count;
    uint32_t corrected_volatile_error_count;
    uint32_t corrected_persistent_error_count;
    uint8_t reserved[0x3d];
} QEMU_PACKED CXLEventMemoryModule;

typedef struct CXLEventDynamicCapacity {
    CXLEventRecordHdr hdr;
    uint8_t type;
    uint8_t validity_flags;
    uint16_t host_id;
    uint8_t updated_region_id;
    uint8_t flags;
    uint8_t reserved2[2];
    uint8_t dynamic_capacity_extent[0x28];
    uint8_t reserved[0x18];
    uint32_t extents_avail;
    uint32_t tags_avail;
} QEMU_PACKED CXLEventDynamicCapacity;

static const QemuUUID dynamic_capacity_uuid = {
    .data = UUID(0xca95afa7, 0xf183, 0x4018, 0x8c, 0x2f,
                 0x95, 0x26, 0x8e, 0x10, 0x1a, 0x2a),
};

typedef enum CXLDCEventType {
    DC_EVENT_ADD_CAPACITY = 0x0,
    DC_EVENT_RELEASE_CAPACITY = 0x1,
    DC_EVENT_FORCED_RELEASE_CAPACITY = 0x2,
    DC_EVENT_REGION_CONFIG_UPDATED = 0x3,
    DC_EVENT_ADD_CAPACITY_RSP = 0x4,
    DC_EVENT_CAPACITY_RELEASED = 0x5,
} CXLDCEventType;

#endif
