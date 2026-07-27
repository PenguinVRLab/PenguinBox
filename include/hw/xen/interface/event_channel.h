/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * event_channel.h
 *
 * Event channels between domains.
 *
 * Copyright (c) 2003-2004, K A Fraser.
 */

#ifndef __XEN_PUBLIC_EVENT_CHANNEL_H__
#define __XEN_PUBLIC_EVENT_CHANNEL_H__

#include "xen.h"

#define EVTCHNOP_bind_interdomain 0
#define EVTCHNOP_bind_virq        1
#define EVTCHNOP_bind_pirq        2
#define EVTCHNOP_close            3
#define EVTCHNOP_send             4
#define EVTCHNOP_status           5
#define EVTCHNOP_alloc_unbound    6
#define EVTCHNOP_bind_ipi         7
#define EVTCHNOP_bind_vcpu        8
#define EVTCHNOP_unmask           9
#define EVTCHNOP_reset           10
#define EVTCHNOP_init_control    11
#define EVTCHNOP_expand_array    12
#define EVTCHNOP_set_priority    13
#ifdef __XEN__
#define EVTCHNOP_reset_cont      14
#endif

typedef uint32_t evtchn_port_t;
DEFINE_XEN_GUEST_HANDLE(evtchn_port_t);

struct evtchn_alloc_unbound {
    domid_t dom, remote_dom;
    evtchn_port_t port;
};
typedef struct evtchn_alloc_unbound evtchn_alloc_unbound_t;

struct evtchn_bind_interdomain {
    domid_t remote_dom;
    evtchn_port_t remote_port;
    evtchn_port_t local_port;
};
typedef struct evtchn_bind_interdomain evtchn_bind_interdomain_t;

struct evtchn_bind_virq {
    uint32_t virq;
    uint32_t vcpu;
    evtchn_port_t port;
};
typedef struct evtchn_bind_virq evtchn_bind_virq_t;

struct evtchn_bind_pirq {
    uint32_t pirq;
#define BIND_PIRQ__WILL_SHARE 1
    uint32_t flags;
    evtchn_port_t port;
};
typedef struct evtchn_bind_pirq evtchn_bind_pirq_t;

struct evtchn_bind_ipi {
    uint32_t vcpu;
    evtchn_port_t port;
};
typedef struct evtchn_bind_ipi evtchn_bind_ipi_t;

struct evtchn_close {
    evtchn_port_t port;
};
typedef struct evtchn_close evtchn_close_t;

struct evtchn_send {
    evtchn_port_t port;
};
typedef struct evtchn_send evtchn_send_t;

struct evtchn_status {
    domid_t  dom;
    evtchn_port_t port;
#define EVTCHNSTAT_closed       0
#define EVTCHNSTAT_unbound      1
#define EVTCHNSTAT_interdomain  2
#define EVTCHNSTAT_pirq         3
#define EVTCHNSTAT_virq         4
#define EVTCHNSTAT_ipi          5
    uint32_t status;
    uint32_t vcpu;
    union {
        struct {
            domid_t dom;
        } unbound;
        struct {
            domid_t dom;
            evtchn_port_t port;
        } interdomain;
        uint32_t pirq;
        uint32_t virq;
    } u;
};
typedef struct evtchn_status evtchn_status_t;

struct evtchn_bind_vcpu {
    evtchn_port_t port;
    uint32_t vcpu;
};
typedef struct evtchn_bind_vcpu evtchn_bind_vcpu_t;

struct evtchn_unmask {
    evtchn_port_t port;
};
typedef struct evtchn_unmask evtchn_unmask_t;

struct evtchn_reset {
    domid_t dom;
};
typedef struct evtchn_reset evtchn_reset_t;

struct evtchn_init_control {
    uint64_t control_gfn;
    uint32_t offset;
    uint32_t vcpu;
    uint8_t link_bits;
    uint8_t _pad[7];
};
typedef struct evtchn_init_control evtchn_init_control_t;

struct evtchn_expand_array {
    uint64_t array_gfn;
};
typedef struct evtchn_expand_array evtchn_expand_array_t;

struct evtchn_set_priority {
    evtchn_port_t port;
    uint32_t priority;
};
typedef struct evtchn_set_priority evtchn_set_priority_t;

struct evtchn_op {
    uint32_t cmd;
    union {
        evtchn_alloc_unbound_t    alloc_unbound;
        evtchn_bind_interdomain_t bind_interdomain;
        evtchn_bind_virq_t        bind_virq;
        evtchn_bind_pirq_t        bind_pirq;
        evtchn_bind_ipi_t         bind_ipi;
        evtchn_close_t            close;
        evtchn_send_t             send;
        evtchn_status_t           status;
        evtchn_bind_vcpu_t        bind_vcpu;
        evtchn_unmask_t           unmask;
    } u;
};
typedef struct evtchn_op evtchn_op_t;
DEFINE_XEN_GUEST_HANDLE(evtchn_op_t);

#define EVTCHN_2L_NR_CHANNELS (sizeof(xen_ulong_t) * sizeof(xen_ulong_t) * 64)

#define EVTCHN_FIFO_PRIORITY_MAX     0
#define EVTCHN_FIFO_PRIORITY_DEFAULT 7
#define EVTCHN_FIFO_PRIORITY_MIN     15

#define EVTCHN_FIFO_MAX_QUEUES (EVTCHN_FIFO_PRIORITY_MIN + 1)

typedef uint32_t event_word_t;

#define EVTCHN_FIFO_PENDING 31
#define EVTCHN_FIFO_MASKED  30
#define EVTCHN_FIFO_LINKED  29
#define EVTCHN_FIFO_BUSY    28

#define EVTCHN_FIFO_LINK_BITS 17
#define EVTCHN_FIFO_LINK_MASK ((1 << EVTCHN_FIFO_LINK_BITS) - 1)

#define EVTCHN_FIFO_NR_CHANNELS (1 << EVTCHN_FIFO_LINK_BITS)

struct evtchn_fifo_control_block {
    uint32_t ready;
    uint32_t _rsvd;
    uint32_t head[EVTCHN_FIFO_MAX_QUEUES];
};
typedef struct evtchn_fifo_control_block evtchn_fifo_control_block_t;

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
