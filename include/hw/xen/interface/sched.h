/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * sched.h
 *
 * Scheduler state interactions
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_SCHED_H__
#define __XEN_PUBLIC_SCHED_H__

#include "event_channel.h"

#define SCHEDOP_yield       0

#define SCHEDOP_block       1

#define SCHEDOP_shutdown    2

#define SCHEDOP_poll        3

#define SCHEDOP_remote_shutdown        4

#define SCHEDOP_shutdown_code 5

#define SCHEDOP_watchdog    6

#define SCHEDOP_pin_override 7

struct sched_shutdown {
    unsigned int reason;
};
typedef struct sched_shutdown sched_shutdown_t;
DEFINE_XEN_GUEST_HANDLE(sched_shutdown_t);

struct sched_poll {
    XEN_GUEST_HANDLE(evtchn_port_t) ports;
    unsigned int nr_ports;
    uint64_t timeout;
};
typedef struct sched_poll sched_poll_t;
DEFINE_XEN_GUEST_HANDLE(sched_poll_t);

struct sched_remote_shutdown {
    domid_t domain_id;
    unsigned int reason;
};
typedef struct sched_remote_shutdown sched_remote_shutdown_t;
DEFINE_XEN_GUEST_HANDLE(sched_remote_shutdown_t);

struct sched_watchdog {
    uint32_t id;
    uint32_t timeout;
};
typedef struct sched_watchdog sched_watchdog_t;
DEFINE_XEN_GUEST_HANDLE(sched_watchdog_t);

struct sched_pin_override {
    int32_t pcpu;
};
typedef struct sched_pin_override sched_pin_override_t;
DEFINE_XEN_GUEST_HANDLE(sched_pin_override_t);

#define SHUTDOWN_poweroff   0
#define SHUTDOWN_reboot     1
#define SHUTDOWN_suspend    2
#define SHUTDOWN_crash      3
#define SHUTDOWN_watchdog   4

#define SHUTDOWN_soft_reset 5
#define SHUTDOWN_MAX        5

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
