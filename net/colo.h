/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef NET_COLO_H
#define NET_COLO_H

#include "qemu/jhash.h"
#include "qemu/timer.h"
#include "net/eth.h"
#include "standard-headers/linux/virtio_net.h"

#define HASHTABLE_MAX_SIZE 16384

#ifndef IPPROTO_DCCP
#define IPPROTO_DCCP 33
#endif

#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP 132
#endif

#ifndef IPPROTO_UDPLITE
#define IPPROTO_UDPLITE 136
#endif

typedef struct Packet {
    void *data;
    union {
        uint8_t *network_header;
        struct ip *ip;
    };
    uint8_t *transport_header;
    int size;
    int64_t creation_ms;
    uint32_t vnet_hdr_len;
    uint32_t tcp_seq;
    uint32_t tcp_ack;
    uint32_t seq_end;
    uint8_t header_size;
    uint16_t payload_size;
    uint16_t offset;
    uint8_t flags;
} Packet;

typedef struct ConnectionKey {
    struct in_addr src;
    struct in_addr dst;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t ip_proto;
} QEMU_PACKED ConnectionKey;

typedef struct Connection {
    GQueue primary_list;
    GQueue secondary_list;
    bool processing;
    uint8_t ip_proto;
    uint32_t compare_seq;
    uint32_t pack;
    uint32_t sack;
    uint32_t  offset;

    int tcp_state;
    uint32_t fin_ack_seq;
} Connection;

uint32_t connection_key_hash(const void *opaque);
int connection_key_equal(const void *opaque1, const void *opaque2);
int parse_packet_early(Packet *pkt);
void extract_ip_and_port(uint32_t tmp_ports, ConnectionKey *key,
                         Packet *pkt, bool reverse);
void fill_connection_key(Packet *pkt, ConnectionKey *key, bool reverse);
Connection *connection_new(ConnectionKey *key);
void connection_destroy(void *opaque);
Connection *connection_get(GHashTable *connection_track_table,
                           ConnectionKey *key,
                           GQueue *conn_list);
bool connection_has_tracked(GHashTable *connection_track_table,
                            ConnectionKey *key);
void connection_hashtable_reset(GHashTable *connection_track_table);
Packet *packet_new(const void *data, int size, int vnet_hdr_len);
Packet *packet_new_nocopy(void *data, int size, int vnet_hdr_len);
void packet_destroy(void *opaque, void *user_data);
void packet_destroy_partial(void *opaque, void *user_data);

#endif
