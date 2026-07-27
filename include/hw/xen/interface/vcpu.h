/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * vcpu.h
 *
 * VCPU initialisation, query, and hotplug.
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_VCPU_H__
#define __XEN_PUBLIC_VCPU_H__

#include "xen.h"

#define VCPUOP_initialise            0

#define VCPUOP_up                    1

#define VCPUOP_down                  2

#define VCPUOP_is_up                 3

#define VCPUOP_get_runstate_info     4
struct vcpu_runstate_info {
    int      state;
    uint64_t state_entry_time;
#define XEN_RUNSTATE_UPDATE          (xen_mk_ullong(1) << 63)
    uint64_t time[4];
};
typedef struct vcpu_runstate_info vcpu_runstate_info_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_runstate_info_t);

#define RUNSTATE_running  0

#define RUNSTATE_runnable 1

#define RUNSTATE_blocked  2

#define RUNSTATE_offline  3

#define VCPUOP_register_runstate_memory_area 5
struct vcpu_register_runstate_memory_area {
    union {
        XEN_GUEST_HANDLE(vcpu_runstate_info_t) h;
        struct vcpu_runstate_info *v;
        uint64_t p;
    } addr;
};
typedef struct vcpu_register_runstate_memory_area vcpu_register_runstate_memory_area_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_register_runstate_memory_area_t);

#define VCPUOP_set_periodic_timer    6
#define VCPUOP_stop_periodic_timer   7
struct vcpu_set_periodic_timer {
    uint64_t period_ns;
};
typedef struct vcpu_set_periodic_timer vcpu_set_periodic_timer_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_set_periodic_timer_t);

#define VCPUOP_set_singleshot_timer  8
#define VCPUOP_stop_singleshot_timer 9
struct vcpu_set_singleshot_timer {
    uint64_t timeout_abs_ns;
    uint32_t flags;
};
typedef struct vcpu_set_singleshot_timer vcpu_set_singleshot_timer_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_set_singleshot_timer_t);

#define _VCPU_SSHOTTMR_future (0)
#define VCPU_SSHOTTMR_future  (1U << _VCPU_SSHOTTMR_future)

#define VCPUOP_register_vcpu_info   10
struct vcpu_register_vcpu_info {
    uint64_t mfn;
    uint32_t offset;
    uint32_t rsvd;
};
typedef struct vcpu_register_vcpu_info vcpu_register_vcpu_info_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_register_vcpu_info_t);

#define VCPUOP_send_nmi             11

#define VCPUOP_get_physid           12
struct vcpu_get_physid {
    uint64_t phys_id;
};
typedef struct vcpu_get_physid vcpu_get_physid_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_get_physid_t);
#define xen_vcpu_physid_to_x86_apicid(physid) ((uint32_t)(physid))
#define xen_vcpu_physid_to_x86_acpiid(physid) ((uint32_t)((physid) >> 32))

#define VCPUOP_register_vcpu_time_memory_area   13
DEFINE_XEN_GUEST_HANDLE(vcpu_time_info_t);
struct vcpu_register_time_memory_area {
    union {
        XEN_GUEST_HANDLE(vcpu_time_info_t) h;
        struct vcpu_time_info *v;
        uint64_t p;
    } addr;
};
typedef struct vcpu_register_time_memory_area vcpu_register_time_memory_area_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_register_time_memory_area_t);

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
