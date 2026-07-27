/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * netif.h
 *
 * Unified network-device I/O interface for Xen guest OSes.
 *
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_NETIF_H__
#define __XEN_PUBLIC_IO_NETIF_H__

#include "ring.h"
#include "../grant_table.h"

#define XEN_NETIF_NR_SLOTS_MIN 18

#define _XEN_NETIF_CTRL_HASH_TYPE_IPV4 0
#define XEN_NETIF_CTRL_HASH_TYPE_IPV4 \
    (1 << _XEN_NETIF_CTRL_HASH_TYPE_IPV4)

#define _XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP 1
#define XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP \
    (1 << _XEN_NETIF_CTRL_HASH_TYPE_IPV4_TCP)

#define _XEN_NETIF_CTRL_HASH_TYPE_IPV6 2
#define XEN_NETIF_CTRL_HASH_TYPE_IPV6 \
    (1 << _XEN_NETIF_CTRL_HASH_TYPE_IPV6)

#define _XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP 3
#define XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP \
    (1 << _XEN_NETIF_CTRL_HASH_TYPE_IPV6_TCP)

#define XEN_NETIF_CTRL_HASH_ALGORITHM_NONE 0

#define XEN_NETIF_CTRL_HASH_ALGORITHM_TOEPLITZ 1

#ifdef XEN_NETIF_DEFINE_TOEPLITZ
static uint32_t xen_netif_toeplitz_hash(const uint8_t *key,
                                        unsigned int keylen,
                                        const uint8_t *buf,
                                        unsigned int buflen)
{
    unsigned int keyi, bufi;
    uint64_t prefix = 0;
    uint64_t hash = 0;

    for (keyi = 0; keyi < 8; keyi++) {
        prefix <<= 8;
        prefix |= (keyi < keylen) ? key[keyi] : 0;
    }

    for (bufi = 0; bufi < buflen; bufi++) {
        uint8_t byte = buf[bufi];
        unsigned int bit;

        for (bit = 0; bit < 8; bit++) {
            if (byte & 0x80)
                hash ^= prefix;
            prefix <<= 1;
            byte <<=1;
        }

        prefix |= (keyi < keylen) ? key[keyi] : 0;
        keyi++;
    }

    return hash >> 32;
}
#endif

struct xen_netif_ctrl_request {
    uint16_t id;
    uint16_t type;

#define XEN_NETIF_CTRL_TYPE_INVALID               0
#define XEN_NETIF_CTRL_TYPE_GET_HASH_FLAGS        1
#define XEN_NETIF_CTRL_TYPE_SET_HASH_FLAGS        2
#define XEN_NETIF_CTRL_TYPE_SET_HASH_KEY          3
#define XEN_NETIF_CTRL_TYPE_GET_HASH_MAPPING_SIZE 4
#define XEN_NETIF_CTRL_TYPE_SET_HASH_MAPPING_SIZE 5
#define XEN_NETIF_CTRL_TYPE_SET_HASH_MAPPING      6
#define XEN_NETIF_CTRL_TYPE_SET_HASH_ALGORITHM    7
#define XEN_NETIF_CTRL_TYPE_GET_GREF_MAPPING_SIZE 8
#define XEN_NETIF_CTRL_TYPE_ADD_GREF_MAPPING      9
#define XEN_NETIF_CTRL_TYPE_DEL_GREF_MAPPING     10

    uint32_t data[3];
};

struct xen_netif_ctrl_response {
    uint16_t id;
    uint16_t type;
    uint32_t status;

#define XEN_NETIF_CTRL_STATUS_SUCCESS           0
#define XEN_NETIF_CTRL_STATUS_NOT_SUPPORTED     1
#define XEN_NETIF_CTRL_STATUS_INVALID_PARAMETER 2
#define XEN_NETIF_CTRL_STATUS_BUFFER_OVERFLOW   3

    uint32_t data;
};

struct xen_netif_gref {
       grant_ref_t ref;
       uint16_t flags;

#define _XEN_NETIF_CTRLF_GREF_readonly    0
#define XEN_NETIF_CTRLF_GREF_readonly    (1U<<_XEN_NETIF_CTRLF_GREF_readonly)

       uint16_t status;
};

DEFINE_RING_TYPES(xen_netif_ctrl,
                  struct xen_netif_ctrl_request,
                  struct xen_netif_ctrl_response);

#define _NETTXF_csum_blank     (0)
#define  NETTXF_csum_blank     (1U<<_NETTXF_csum_blank)

#define _NETTXF_data_validated (1)
#define  NETTXF_data_validated (1U<<_NETTXF_data_validated)

#define _NETTXF_more_data      (2)
#define  NETTXF_more_data      (1U<<_NETTXF_more_data)

#define _NETTXF_extra_info     (3)
#define  NETTXF_extra_info     (1U<<_NETTXF_extra_info)

#define XEN_NETIF_MAX_TX_SIZE 0xFFFF
struct netif_tx_request {
    grant_ref_t gref;
    uint16_t offset;
    uint16_t flags;
    uint16_t id;
    uint16_t size;
};
typedef struct netif_tx_request netif_tx_request_t;

#define XEN_NETIF_EXTRA_TYPE_NONE      (0)
#define XEN_NETIF_EXTRA_TYPE_GSO       (1)
#define XEN_NETIF_EXTRA_TYPE_MCAST_ADD (2)
#define XEN_NETIF_EXTRA_TYPE_MCAST_DEL (3)
#define XEN_NETIF_EXTRA_TYPE_HASH      (4)
#define XEN_NETIF_EXTRA_TYPE_MAX       (5)

#define _XEN_NETIF_EXTRA_FLAG_MORE (0)
#define XEN_NETIF_EXTRA_FLAG_MORE  (1U<<_XEN_NETIF_EXTRA_FLAG_MORE)

#define XEN_NETIF_GSO_TYPE_NONE         (0)
#define XEN_NETIF_GSO_TYPE_TCPV4        (1)
#define XEN_NETIF_GSO_TYPE_TCPV6        (2)

struct netif_extra_info {
    uint8_t type;
    uint8_t flags;
    union {
        struct {
            uint16_t size;
            uint8_t type;
            uint8_t pad;
            uint16_t features;
        } gso;
        struct {
            uint8_t addr[6];
        } mcast;
        struct {
            uint8_t type;
            uint8_t algorithm;
            uint8_t value[4];
        } hash;
        uint16_t pad[3];
    } u;
};
typedef struct netif_extra_info netif_extra_info_t;

struct netif_tx_response {
    uint16_t id;
    int16_t  status;
};
typedef struct netif_tx_response netif_tx_response_t;

struct netif_rx_request {
    uint16_t    id;
    uint16_t    pad;
    grant_ref_t gref;
};
typedef struct netif_rx_request netif_rx_request_t;

#define _NETRXF_data_validated (0)
#define  NETRXF_data_validated (1U<<_NETRXF_data_validated)

#define _NETRXF_csum_blank     (1)
#define  NETRXF_csum_blank     (1U<<_NETRXF_csum_blank)

#define _NETRXF_more_data      (2)
#define  NETRXF_more_data      (1U<<_NETRXF_more_data)

#define _NETRXF_extra_info     (3)
#define  NETRXF_extra_info     (1U<<_NETRXF_extra_info)

#define _NETRXF_gso_prefix     (4)
#define  NETRXF_gso_prefix     (1U<<_NETRXF_gso_prefix)

struct netif_rx_response {
    uint16_t id;
    uint16_t offset;
    uint16_t flags;
    int16_t  status;
};
typedef struct netif_rx_response netif_rx_response_t;

DEFINE_RING_TYPES(netif_tx, struct netif_tx_request, struct netif_tx_response);
DEFINE_RING_TYPES(netif_rx, struct netif_rx_request, struct netif_rx_response);

#define NETIF_RSP_DROPPED         -2
#define NETIF_RSP_ERROR           -1
#define NETIF_RSP_OKAY             0
#define NETIF_RSP_NULL             1

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
