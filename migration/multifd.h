/*
 * Multifd common functions
 *
 * Copyright (c) 2019-2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_MULTIFD_H
#define QEMU_MIGRATION_MULTIFD_H

#include "exec/target_page.h"
#include "ram.h"

typedef struct MultiFDRecvData MultiFDRecvData;
typedef struct MultiFDSendData MultiFDSendData;

typedef enum {
    MULTIFD_SYNC_NONE = 0,
    MULTIFD_SYNC_LOCAL,
    MULTIFD_SYNC_ALL,
} MultiFDSyncReq;

bool multifd_send_setup(void);
void multifd_send_shutdown(void);
void multifd_send_channel_created(void);
int multifd_recv_setup(Error **errp);
void multifd_recv_cleanup(void);
void multifd_recv_shutdown(void);
bool multifd_recv_all_channels_created(void);
void multifd_recv_new_channel(QIOChannel *ioc, Error **errp);
void multifd_recv_sync_main(void);
int multifd_send_sync_main(MultiFDSyncReq req);
bool multifd_queue_page(RAMBlock *block, ram_addr_t offset);
bool multifd_recv(void);
MultiFDRecvData *multifd_get_recv_data(void);

#define MULTIFD_MAGIC 0x11223344U
#define MULTIFD_VERSION 1

#define MULTIFD_FLAG_SYNC (1 << 0)

#define MULTIFD_FLAG_COMPRESSION_MASK (0x1f << 1)
#define MULTIFD_FLAG_NOCOMP (0 << 1)
#define MULTIFD_FLAG_ZLIB (1 << 1)
#define MULTIFD_FLAG_ZSTD (2 << 1)
#define MULTIFD_FLAG_QPL (4 << 1)
#define MULTIFD_FLAG_UADK (8 << 1)
#define MULTIFD_FLAG_QATZIP (16 << 1)

#define MULTIFD_FLAG_DEVICE_STATE (32 << 1)

#define MULTIFD_PACKET_SIZE (512 * 1024)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
} __attribute__((packed)) MultiFDPacketHdr_t;

typedef struct {
    MultiFDPacketHdr_t hdr;

    uint32_t pages_alloc;
    uint32_t normal_pages;
    uint32_t next_packet_size;
    uint64_t packet_num;
    uint32_t zero_pages;
    uint32_t unused32[1];
    uint64_t unused64[3];
    char ramblock[256];
    uint64_t offset[];
} __attribute__((packed)) MultiFDPacket_t;

typedef struct {
    MultiFDPacketHdr_t hdr;

    char idstr[256];
    uint32_t instance_id;

    uint32_t next_packet_size;
} __attribute__((packed)) MultiFDPacketDeviceState_t;

typedef struct {
    uint32_t num;
    uint32_t normal_num;
    RAMBlock *block;
    ram_addr_t *offset;
} MultiFDPages_t;

struct MultiFDRecvData {
    void *opaque;
    size_t size;
    off_t file_offset;
};

typedef struct {
    char *idstr;
    uint32_t instance_id;
    char *buf;
    size_t buf_len;
} MultiFDDeviceState_t;

typedef enum {
    MULTIFD_PAYLOAD_NONE,
    MULTIFD_PAYLOAD_RAM,
    MULTIFD_PAYLOAD_DEVICE_STATE,
} MultiFDPayloadType;

typedef struct MultiFDPayload {
    MultiFDPages_t ram;
    MultiFDDeviceState_t device_state;
} MultiFDPayload;

struct MultiFDSendData {
    MultiFDPayloadType type;
    MultiFDPayload u;
};

static inline bool multifd_payload_empty(MultiFDSendData *data)
{
    return data->type == MULTIFD_PAYLOAD_NONE;
}

static inline bool multifd_payload_device_state(MultiFDSendData *data)
{
    return data->type == MULTIFD_PAYLOAD_DEVICE_STATE;
}

static inline void multifd_set_payload_type(MultiFDSendData *data,
                                            MultiFDPayloadType type)
{
    assert(multifd_payload_empty(data));
    assert(type != MULTIFD_PAYLOAD_NONE);

    data->type = type;
}

typedef struct {

    uint8_t id;
    char *name;
    QemuThread thread;
    bool thread_created;
    QemuThread tls_thread;
    bool tls_thread_created;
    QIOChannel *c;
    uint32_t packet_len;
    int write_flags;

    QemuSemaphore sem;
    QemuSemaphore sem_sync;

    uint32_t flags;
    bool pending_job;
    MultiFDSyncReq pending_sync;

    MultiFDSendData *data;

    MultiFDPacket_t *packet;
    MultiFDPacketDeviceState_t *packet_device_state;
    uint32_t next_packet_size;
    uint64_t packets_sent;
    struct iovec *iov;
    uint32_t iovs_num;
    void *compress_data;
}  MultiFDSendParams;

typedef struct {

    uint8_t id;
    char *name;
    QemuThread thread;
    bool thread_created;
    QIOChannel *c;
    uint32_t packet_len;

    QemuSemaphore sem_sync;
    QemuSemaphore sem;

    QemuMutex mutex;
    bool quit;
    uint32_t flags;
    uint64_t packet_num;
    int pending_job;
    MultiFDRecvData *data;

    MultiFDPacket_t *packet;
    MultiFDPacketDeviceState_t *packet_dev_state;
    uint32_t next_packet_size;
    uint64_t packets_recved;
    RAMBlock *block;
    uint8_t *host;
    struct iovec *iov;
    ram_addr_t *normal;
    uint32_t normal_num;
    ram_addr_t *zero;
    uint32_t zero_num;
    void *compress_data;
    int read_flags;
} MultiFDRecvParams;

typedef struct {

    int (*send_setup)(MultiFDSendParams *p, Error **errp);

    void (*send_cleanup)(MultiFDSendParams *p, Error **errp);

    int (*send_prepare)(MultiFDSendParams *p, Error **errp);

    int (*recv_setup)(MultiFDRecvParams *p, Error **errp);

    void (*recv_cleanup)(MultiFDRecvParams *p);

    int (*recv)(MultiFDRecvParams *p, Error **errp);
} MultiFDMethods;

void multifd_register_ops(int method, const MultiFDMethods *ops);
void multifd_send_fill_packet(MultiFDSendParams *p);
bool multifd_send_prepare_common(MultiFDSendParams *p);
void multifd_send_zero_page_detect(MultiFDSendParams *p);
void multifd_recv_zero_page_process(MultiFDRecvParams *p);

void multifd_channel_connect(MultiFDSendParams *p, QIOChannel *ioc);
bool multifd_send(MultiFDSendData **send_data);
MultiFDSendData *multifd_send_data_alloc(void);
void multifd_send_data_clear(MultiFDSendData *data);
void multifd_send_data_free(MultiFDSendData *data);

static inline uint32_t multifd_ram_page_size(void)
{
    return qemu_target_page_size();
}

static inline uint32_t multifd_ram_page_count(void)
{
    return MULTIFD_PACKET_SIZE / qemu_target_page_size();
}

void multifd_ram_save_setup(void);
void multifd_ram_save_cleanup(void);
int multifd_ram_flush_and_sync(QEMUFile *f);
bool multifd_ram_sync_per_round(void);
bool multifd_ram_sync_per_section(void);
void multifd_ram_payload_alloc(MultiFDPages_t *pages);
void multifd_ram_payload_free(MultiFDPages_t *pages);
void multifd_ram_fill_packet(MultiFDSendParams *p);
int multifd_ram_unfill_packet(MultiFDRecvParams *p, Error **errp);

void multifd_send_data_clear_device_state(MultiFDDeviceState_t *device_state);

void multifd_device_state_send_setup(void);
void multifd_device_state_send_cleanup(void);

void multifd_device_state_send_prepare(MultiFDSendParams *p);

#endif
