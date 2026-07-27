/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * memory.h
 *
 * Memory reservation and information.
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_MEMORY_H__
#define __XEN_PUBLIC_MEMORY_H__

#include "xen.h"
#include "physdev.h"

#define XENMEM_increase_reservation 0
#define XENMEM_decrease_reservation 1
#define XENMEM_populate_physmap     6

#if __XEN_INTERFACE_VERSION__ >= 0x00030209
#define XENMEMF_address_bits(x)     (x)
#define XENMEMF_get_address_bits(x) ((x) & 0xffu)
#define XENMEMF_node(x)     (((x) + 1) << 8)
#define XENMEMF_get_node(x) ((((x) >> 8) - 1) & 0xffu)
#define XENMEMF_populate_on_demand (1<<16)
#define XENMEMF_exact_node_request  (1<<17)
#define XENMEMF_exact_node(n) (XENMEMF_node(n) | XENMEMF_exact_node_request)
#define XENMEMF_vnode  (1<<18)
#endif

struct xen_memory_reservation {

    XEN_GUEST_HANDLE(xen_pfn_t) extent_start;

    xen_ulong_t    nr_extents;
    unsigned int   extent_order;

#if __XEN_INTERFACE_VERSION__ >= 0x00030209
    unsigned int   mem_flags;
#else
    unsigned int   address_bits;
#endif

    domid_t        domid;
};
typedef struct xen_memory_reservation xen_memory_reservation_t;
DEFINE_XEN_GUEST_HANDLE(xen_memory_reservation_t);

#define XENMEM_exchange             11
struct xen_memory_exchange {
    struct xen_memory_reservation in;

    struct xen_memory_reservation out;

    xen_ulong_t nr_exchanged;
};
typedef struct xen_memory_exchange xen_memory_exchange_t;
DEFINE_XEN_GUEST_HANDLE(xen_memory_exchange_t);

#define XENMEM_maximum_ram_page     2

struct xen_memory_domain {
    domid_t domid;
};

#define XENMEM_current_reservation  3
#define XENMEM_maximum_reservation  4

#define XENMEM_maximum_gpfn         14

#define XENMEM_machphys_mfn_list    5
struct xen_machphys_mfn_list {
    unsigned int max_extents;

    XEN_GUEST_HANDLE(xen_pfn_t) extent_start;

    unsigned int nr_extents;
};
typedef struct xen_machphys_mfn_list xen_machphys_mfn_list_t;
DEFINE_XEN_GUEST_HANDLE(xen_machphys_mfn_list_t);

#define XENMEM_machphys_compat_mfn_list     25

#define XENMEM_machphys_mapping     12
struct xen_machphys_mapping {
    xen_ulong_t v_start, v_end;
    xen_ulong_t max_mfn;
};
typedef struct xen_machphys_mapping xen_machphys_mapping_t;
DEFINE_XEN_GUEST_HANDLE(xen_machphys_mapping_t);

#define XENMAPSPACE_shared_info  0
#define XENMAPSPACE_grant_table  1
#define XENMAPSPACE_gmfn         2
#define XENMAPSPACE_gmfn_range   3
#define XENMAPSPACE_gmfn_foreign 4
#define XENMAPSPACE_dev_mmio     5

#define XENMEM_add_to_physmap      7
struct xen_add_to_physmap {
    domid_t domid;

    uint16_t    size;

    unsigned int space;

#define XENMAPIDX_grant_table_status 0x80000000

    xen_ulong_t idx;

    xen_pfn_t     gpfn;
};
typedef struct xen_add_to_physmap xen_add_to_physmap_t;
DEFINE_XEN_GUEST_HANDLE(xen_add_to_physmap_t);

#define XENMEM_add_to_physmap_batch 23
struct xen_add_to_physmap_batch {
    domid_t domid;
    uint16_t space;

    uint16_t size;

#if __XEN_INTERFACE_VERSION__ < 0x00040700
    domid_t foreign_domid;
#else
    union xen_add_to_physmap_batch_extra {
        domid_t foreign_domid;
        uint16_t res0;
    } u;
#endif

    XEN_GUEST_HANDLE(xen_ulong_t) idxs;

    XEN_GUEST_HANDLE(xen_pfn_t) gpfns;

    XEN_GUEST_HANDLE(int) errs;
};
typedef struct xen_add_to_physmap_batch xen_add_to_physmap_batch_t;
DEFINE_XEN_GUEST_HANDLE(xen_add_to_physmap_batch_t);

#if __XEN_INTERFACE_VERSION__ < 0x00040400
#define XENMEM_add_to_physmap_range XENMEM_add_to_physmap_batch
#define xen_add_to_physmap_range xen_add_to_physmap_batch
typedef struct xen_add_to_physmap_batch xen_add_to_physmap_range_t;
DEFINE_XEN_GUEST_HANDLE(xen_add_to_physmap_range_t);
#endif

#define XENMEM_remove_from_physmap      15
struct xen_remove_from_physmap {
    domid_t domid;

    xen_pfn_t     gpfn;
};
typedef struct xen_remove_from_physmap xen_remove_from_physmap_t;
DEFINE_XEN_GUEST_HANDLE(xen_remove_from_physmap_t);

#define XENMEM_memory_map           9
struct xen_memory_map {
    unsigned int nr_entries;

    XEN_GUEST_HANDLE(void) buffer;
};
typedef struct xen_memory_map xen_memory_map_t;
DEFINE_XEN_GUEST_HANDLE(xen_memory_map_t);

#define XENMEM_machine_memory_map   10

#define XENMEM_set_memory_map       13
struct xen_foreign_memory_map {
    domid_t domid;
    struct xen_memory_map map;
};
typedef struct xen_foreign_memory_map xen_foreign_memory_map_t;
DEFINE_XEN_GUEST_HANDLE(xen_foreign_memory_map_t);

#define XENMEM_set_pod_target       16
#define XENMEM_get_pod_target       17
struct xen_pod_target {
    uint64_t target_pages;
    uint64_t tot_pages;
    uint64_t pod_cache_pages;
    uint64_t pod_entries;
    domid_t domid;
};
typedef struct xen_pod_target xen_pod_target_t;

#if defined(__XEN__) || defined(__XEN_TOOLS__)

#ifndef uint64_aligned_t
#define uint64_aligned_t uint64_t
#endif

#define XENMEM_get_sharing_freed_pages    18
#define XENMEM_get_sharing_shared_pages   19

#define XENMEM_paging_op                    20
#define XENMEM_paging_op_nominate           0
#define XENMEM_paging_op_evict              1
#define XENMEM_paging_op_prep               2

struct xen_mem_paging_op {
    uint8_t     op;
    domid_t     domain;

    XEN_GUEST_HANDLE_64(const_uint8) buffer;
    uint64_aligned_t    gfn;
};
typedef struct xen_mem_paging_op xen_mem_paging_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_mem_paging_op_t);

#define XENMEM_access_op                    21
#define XENMEM_access_op_set_access         0
#define XENMEM_access_op_get_access         1
#define XENMEM_access_op_set_access_multi   4

typedef enum {
    XENMEM_access_n,
    XENMEM_access_r,
    XENMEM_access_w,
    XENMEM_access_rw,
    XENMEM_access_x,
    XENMEM_access_rx,
    XENMEM_access_wx,
    XENMEM_access_rwx,
    XENMEM_access_rx2rw,
    XENMEM_access_n2rwx,
    XENMEM_access_default
} xenmem_access_t;

struct xen_mem_access_op {
    uint8_t op;
    uint8_t access;
    domid_t domid;
    uint32_t nr;
    uint64_aligned_t pfn;
    XEN_GUEST_HANDLE(const_uint64) pfn_list;
    XEN_GUEST_HANDLE(const_uint8) access_list;
};
typedef struct xen_mem_access_op xen_mem_access_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_mem_access_op_t);

#define XENMEM_sharing_op                   22
#define XENMEM_sharing_op_nominate_gfn      0
#define XENMEM_sharing_op_nominate_gref     1
#define XENMEM_sharing_op_share             2
#define XENMEM_sharing_op_debug_gfn         3
#define XENMEM_sharing_op_debug_mfn         4
#define XENMEM_sharing_op_debug_gref        5
#define XENMEM_sharing_op_add_physmap       6
#define XENMEM_sharing_op_audit             7
#define XENMEM_sharing_op_range_share       8
#define XENMEM_sharing_op_fork              9
#define XENMEM_sharing_op_fork_reset        10

#define XENMEM_SHARING_OP_S_HANDLE_INVALID  (-10)
#define XENMEM_SHARING_OP_C_HANDLE_INVALID  (-9)

#define XENMEM_SHARING_OP_FIELD_IS_GREF_FLAG   (xen_mk_ullong(1) << 62)

#define XENMEM_SHARING_OP_FIELD_MAKE_GREF(field, val)  \
    (field) = (XENMEM_SHARING_OP_FIELD_IS_GREF_FLAG | val)
#define XENMEM_SHARING_OP_FIELD_IS_GREF(field)         \
    ((field) & XENMEM_SHARING_OP_FIELD_IS_GREF_FLAG)
#define XENMEM_SHARING_OP_FIELD_GET_GREF(field)        \
    ((field) & (~XENMEM_SHARING_OP_FIELD_IS_GREF_FLAG))

struct xen_mem_sharing_op {
    uint8_t     op;
    domid_t     domain;

    union {
        struct mem_sharing_op_nominate {
            union {
                uint64_aligned_t gfn;
                uint32_t      grant_ref;
            } u;
            uint64_aligned_t  handle;
        } nominate;
        struct mem_sharing_op_share {
            uint64_aligned_t source_gfn;
            uint64_aligned_t source_handle;
            uint64_aligned_t client_gfn;
            uint64_aligned_t client_handle;
            domid_t  client_domain;
        } share;
        struct mem_sharing_op_range {
            uint64_aligned_t first_gfn;
            uint64_aligned_t last_gfn;
            uint64_aligned_t opaque;
            domid_t client_domain;
            uint16_t _pad[3];
        } range;
        struct mem_sharing_op_debug {
            union {
                uint64_aligned_t gfn;
                uint64_aligned_t mfn;
                uint32_t gref;
            } u;
        } debug;
        struct mem_sharing_op_fork {
            domid_t parent_domain;
#define XENMEM_FORK_WITH_IOMMU_ALLOWED (1u << 0)
#define XENMEM_FORK_BLOCK_INTERRUPTS   (1u << 1)
#define XENMEM_FORK_RESET_STATE        (1u << 2)
#define XENMEM_FORK_RESET_MEMORY       (1u << 3)
            uint16_t flags;
            uint32_t pad;
        } fork;
    } u;
};
typedef struct xen_mem_sharing_op xen_mem_sharing_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_mem_sharing_op_t);

#define XENMEM_claim_pages                  24

#define XENMEM_reserved_device_memory_map   27
struct xen_reserved_device_memory {
    xen_pfn_t start_pfn;
    xen_ulong_t nr_pages;
};
typedef struct xen_reserved_device_memory xen_reserved_device_memory_t;
DEFINE_XEN_GUEST_HANDLE(xen_reserved_device_memory_t);

struct xen_reserved_device_memory_map {
#define XENMEM_RDM_ALL 1
    uint32_t flags;
    unsigned int nr_entries;
    XEN_GUEST_HANDLE(xen_reserved_device_memory_t) buffer;
    union {
        physdev_pci_device_t pci;
    } dev;
};
typedef struct xen_reserved_device_memory_map xen_reserved_device_memory_map_t;
DEFINE_XEN_GUEST_HANDLE(xen_reserved_device_memory_map_t);

#endif

#define XENMEM_acquire_resource 28
struct xen_mem_acquire_resource {
    domid_t domid;
    uint16_t type;

#define XENMEM_resource_ioreq_server 0
#define XENMEM_resource_grant_table 1
#define XENMEM_resource_vmtrace_buf 2

    uint32_t id;

#define XENMEM_resource_grant_table_id_shared 0
#define XENMEM_resource_grant_table_id_status 1

    uint32_t nr_frames;
    uint32_t pad;
    uint64_t frame;

#define XENMEM_resource_ioreq_server_frame_bufioreq 0
#define XENMEM_resource_ioreq_server_frame_ioreq(n) (1 + (n))

    XEN_GUEST_HANDLE(xen_pfn_t) frame_list;
};
typedef struct xen_mem_acquire_resource xen_mem_acquire_resource_t;
DEFINE_XEN_GUEST_HANDLE(xen_mem_acquire_resource_t);

#define XENMEM_get_vnumainfo                26

struct xen_vmemrange {
    uint64_t start, end;
    unsigned int flags;
    unsigned int nid;
};
typedef struct xen_vmemrange xen_vmemrange_t;
DEFINE_XEN_GUEST_HANDLE(xen_vmemrange_t);

struct xen_vnuma_topology_info {
    domid_t domid;
    uint16_t pad;
    unsigned int nr_vnodes;
    unsigned int nr_vcpus;
    unsigned int nr_vmemranges;
    union {
        XEN_GUEST_HANDLE(uint) h;
        uint64_t pad;
    } vdistance;
    union {
        XEN_GUEST_HANDLE(uint) h;
        uint64_t pad;
    } vcpu_to_vnode;
    union {
        XEN_GUEST_HANDLE(xen_vmemrange_t) h;
        uint64_t pad;
    } vmemrange;
};
typedef struct xen_vnuma_topology_info xen_vnuma_topology_info_t;
DEFINE_XEN_GUEST_HANDLE(xen_vnuma_topology_info_t);

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
