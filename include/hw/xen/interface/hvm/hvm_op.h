/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2007, Keir Fraser
 */

#ifndef __XEN_PUBLIC_HVM_HVM_OP_H__
#define __XEN_PUBLIC_HVM_HVM_OP_H__

#include "../xen.h"
#include "../trace.h"
#include "../event_channel.h"

#define HVMOP_set_param           0
#define HVMOP_get_param           1
struct xen_hvm_param {
    domid_t  domid;
    uint16_t pad;
    uint32_t index;
    uint64_t value;
};
typedef struct xen_hvm_param xen_hvm_param_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_param_t);

struct xen_hvm_altp2m_suppress_ve {
    uint16_t view;
    uint8_t suppress_ve;
    uint8_t pad1;
    uint32_t pad2;
    uint64_t gfn;
};

struct xen_hvm_altp2m_suppress_ve_multi {
    uint16_t view;
    uint8_t suppress_ve;
    uint8_t pad1;
    int32_t first_error;
    uint64_t first_gfn;
    uint64_t last_gfn;
    uint64_t first_error_gfn;
};

#if __XEN_INTERFACE_VERSION__ < 0x00040900

#define HVMOP_set_pci_intx_level  2
struct xen_hvm_set_pci_intx_level {
    domid_t  domid;
    uint8_t  domain, bus, device, intx;
    uint8_t  level;
};
typedef struct xen_hvm_set_pci_intx_level xen_hvm_set_pci_intx_level_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_pci_intx_level_t);

#define HVMOP_set_isa_irq_level   3
struct xen_hvm_set_isa_irq_level {
    domid_t  domid;
    uint8_t  isa_irq;
    uint8_t  level;
};
typedef struct xen_hvm_set_isa_irq_level xen_hvm_set_isa_irq_level_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_isa_irq_level_t);

#define HVMOP_set_pci_link_route  4
struct xen_hvm_set_pci_link_route {
    domid_t  domid;
    uint8_t  link;
    uint8_t  isa_irq;
};
typedef struct xen_hvm_set_pci_link_route xen_hvm_set_pci_link_route_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_pci_link_route_t);

#endif

#define HVMOP_flush_tlbs          5

#ifndef XEN_GENERATING_COMPAT_HEADERS

typedef enum {
    HVMMEM_ram_rw,
    HVMMEM_ram_ro,
    HVMMEM_mmio_dm,
#if __XEN_INTERFACE_VERSION__ < 0x00040700
    HVMMEM_mmio_write_dm,
#else
    HVMMEM_unused,
#endif
    HVMMEM_ioreq_server
} hvmmem_type_t;

#endif

#define HVMOP_pagetable_dying        9
struct xen_hvm_pagetable_dying {
    domid_t  domid;
    uint16_t pad[3];
    uint64_t gpa;
};
typedef struct xen_hvm_pagetable_dying xen_hvm_pagetable_dying_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_pagetable_dying_t);

#define HVMOP_get_time              10
struct xen_hvm_get_time {
    uint64_t now;
};
typedef struct xen_hvm_get_time xen_hvm_get_time_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_get_time_t);

#define HVMOP_xentrace              11
struct xen_hvm_xentrace {
    uint16_t event, extra_bytes;
    uint8_t extra[TRACE_EXTRA_MAX * sizeof(uint32_t)];
};
typedef struct xen_hvm_xentrace xen_hvm_xentrace_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_xentrace_t);

#if defined(__XEN__) || defined(__XEN_TOOLS__)

#define HVMOP_set_mem_access        12

#define HVMOP_get_mem_access        13

#endif

#define HVMOP_get_mem_type    15
struct xen_hvm_get_mem_type {
    domid_t domid;
    uint16_t mem_type;
    uint16_t pad[2];
    uint64_t pfn;
};
typedef struct xen_hvm_get_mem_type xen_hvm_get_mem_type_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_get_mem_type_t);

#if defined(__XEN__) || defined(__XEN_TOOLS__)

#define HVM_IOREQSRV_BUFIOREQ_OFF    0
#define HVM_IOREQSRV_BUFIOREQ_LEGACY 1
#define HVM_IOREQSRV_BUFIOREQ_ATOMIC 2

#endif

#if defined(__i386__) || defined(__x86_64__)

#define HVMOP_set_evtchn_upcall_vector 23
struct xen_hvm_evtchn_upcall_vector {
    uint32_t vcpu;
    uint8_t vector;
};
typedef struct xen_hvm_evtchn_upcall_vector xen_hvm_evtchn_upcall_vector_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_evtchn_upcall_vector_t);

#endif

#define HVMOP_guest_request_vm_event 24

#define HVMOP_altp2m 25

#define HVMOP_ALTP2M_INTERFACE_VERSION 0x00000001

struct xen_hvm_altp2m_domain_state {
    uint8_t state;
};
typedef struct xen_hvm_altp2m_domain_state xen_hvm_altp2m_domain_state_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_domain_state_t);

struct xen_hvm_altp2m_vcpu_enable_notify {
    uint32_t vcpu_id;
    uint32_t pad;
    uint64_t gfn;
};
typedef struct xen_hvm_altp2m_vcpu_enable_notify xen_hvm_altp2m_vcpu_enable_notify_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_vcpu_enable_notify_t);

struct xen_hvm_altp2m_vcpu_disable_notify {
    uint32_t vcpu_id;
};
typedef struct xen_hvm_altp2m_vcpu_disable_notify xen_hvm_altp2m_vcpu_disable_notify_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_vcpu_disable_notify_t);

struct xen_hvm_altp2m_view {
    uint16_t view;
    uint16_t hvmmem_default_access;
};
typedef struct xen_hvm_altp2m_view xen_hvm_altp2m_view_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_view_t);

#if __XEN_INTERFACE_VERSION__ < 0x00040a00
struct xen_hvm_altp2m_set_mem_access {
    uint16_t view;
    uint16_t access;
    uint32_t pad;
    uint64_t gfn;
};
typedef struct xen_hvm_altp2m_set_mem_access xen_hvm_altp2m_set_mem_access_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_set_mem_access_t);
#endif

struct xen_hvm_altp2m_mem_access {
    uint16_t view;
    uint16_t access;
    uint32_t pad;
    uint64_t gfn;
};
typedef struct xen_hvm_altp2m_mem_access xen_hvm_altp2m_mem_access_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_mem_access_t);

struct xen_hvm_altp2m_set_mem_access_multi {
    uint16_t view;
    uint16_t pad;
    uint32_t nr;
    uint64_t opaque;
    XEN_GUEST_HANDLE(const_uint64) pfn_list;
    XEN_GUEST_HANDLE(const_uint8) access_list;
};

struct xen_hvm_altp2m_change_gfn {
    uint16_t view;
    uint16_t pad1;
    uint32_t pad2;
    uint64_t old_gfn;
    uint64_t new_gfn;
};
typedef struct xen_hvm_altp2m_change_gfn xen_hvm_altp2m_change_gfn_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_change_gfn_t);

struct xen_hvm_altp2m_get_vcpu_p2m_idx {
    uint32_t vcpu_id;
    uint16_t altp2m_idx;
};

struct xen_hvm_altp2m_set_visibility {
    uint16_t altp2m_idx;
    uint8_t visible;
    uint8_t pad;
};

struct xen_hvm_altp2m_op {
    uint32_t version;
    uint32_t cmd;
#define HVMOP_altp2m_get_domain_state     1
#define HVMOP_altp2m_set_domain_state     2
#define HVMOP_altp2m_vcpu_enable_notify   3
#define HVMOP_altp2m_create_p2m           4
#define HVMOP_altp2m_destroy_p2m          5
#define HVMOP_altp2m_switch_p2m           6
#define HVMOP_altp2m_set_mem_access       7
#define HVMOP_altp2m_change_gfn           8
#define HVMOP_altp2m_set_mem_access_multi 9
#define HVMOP_altp2m_set_suppress_ve      10
#define HVMOP_altp2m_get_suppress_ve      11
#define HVMOP_altp2m_get_mem_access       12
#define HVMOP_altp2m_vcpu_disable_notify  13
#define HVMOP_altp2m_get_p2m_idx          14
#define HVMOP_altp2m_set_suppress_ve_multi 15
#define HVMOP_altp2m_set_visibility       16
    domid_t domain;
    uint16_t pad1;
    uint32_t pad2;
    union {
        struct xen_hvm_altp2m_domain_state         domain_state;
        struct xen_hvm_altp2m_vcpu_enable_notify   enable_notify;
        struct xen_hvm_altp2m_view                 view;
#if __XEN_INTERFACE_VERSION__ < 0x00040a00
        struct xen_hvm_altp2m_set_mem_access       set_mem_access;
#endif
        struct xen_hvm_altp2m_mem_access           mem_access;
        struct xen_hvm_altp2m_change_gfn           change_gfn;
        struct xen_hvm_altp2m_set_mem_access_multi set_mem_access_multi;
        struct xen_hvm_altp2m_suppress_ve          suppress_ve;
        struct xen_hvm_altp2m_suppress_ve_multi    suppress_ve_multi;
        struct xen_hvm_altp2m_vcpu_disable_notify  disable_notify;
        struct xen_hvm_altp2m_get_vcpu_p2m_idx     get_vcpu_p2m_idx;
        struct xen_hvm_altp2m_set_visibility       set_visibility;
        uint8_t pad[64];
    } u;
};
typedef struct xen_hvm_altp2m_op xen_hvm_altp2m_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_op_t);

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
