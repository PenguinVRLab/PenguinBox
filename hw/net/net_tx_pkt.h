/*
 * QEMU TX packets abstraction
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Tamir Shomer <tamirs@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef NET_TX_PKT_H
#define NET_TX_PKT_H

#include "net/eth.h"
#include "exec/hwaddr.h"

struct NetTxPkt;

typedef void (*NetTxPktFreeFrag)(void *, void *, size_t);
typedef void (*NetTxPktSend)(void *, const struct iovec *, int, const struct iovec *, int);

void net_tx_pkt_init(struct NetTxPkt **pkt, uint32_t max_frags);

void net_tx_pkt_uninit(struct NetTxPkt *pkt);

struct virtio_net_hdr *net_tx_pkt_get_vhdr(struct NetTxPkt *pkt);

bool net_tx_pkt_build_vheader(struct NetTxPkt *pkt, bool tso_enable,
    bool csum_enable, uint32_t gso_size);

void net_tx_pkt_setup_vlan_header_ex(struct NetTxPkt *pkt,
    uint16_t vlan, uint16_t vlan_ethtype);

static inline void
net_tx_pkt_setup_vlan_header(struct NetTxPkt *pkt, uint16_t vlan)
{
    net_tx_pkt_setup_vlan_header_ex(pkt, vlan, ETH_P_VLAN);
}

bool net_tx_pkt_add_raw_fragment(struct NetTxPkt *pkt, void *base, size_t len);

void net_tx_pkt_update_ip_checksums(struct NetTxPkt *pkt);

void net_tx_pkt_update_ip_hdr_checksum(struct NetTxPkt *pkt);

bool net_tx_pkt_update_sctp_checksum(struct NetTxPkt *pkt);

size_t net_tx_pkt_get_total_len(struct NetTxPkt *pkt);

eth_pkt_types_e net_tx_pkt_get_packet_type(struct NetTxPkt *pkt);

void net_tx_pkt_dump(struct NetTxPkt *pkt);

void net_tx_pkt_reset(struct NetTxPkt *pkt,
                      NetTxPktFreeFrag callback, void *context);

void net_tx_pkt_unmap_frag_pci(void *context, void *base, size_t len);

bool net_tx_pkt_add_raw_fragment_pci(struct NetTxPkt *pkt, PCIDevice *pci_dev,
                                     dma_addr_t pa, size_t len);

bool net_tx_pkt_send(struct NetTxPkt *pkt, NetClientState *nc);

bool net_tx_pkt_send_custom(struct NetTxPkt *pkt, bool offload,
                            NetTxPktSend callback, void *context);

bool net_tx_pkt_parse(struct NetTxPkt *pkt);

bool net_tx_pkt_has_fragments(struct NetTxPkt *pkt);

void net_tx_pkt_fix_ip6_payload_len(struct NetTxPkt *pkt);

#endif
