/*
 * Copyright (c) 2015 FUJITSU LIMITED
 * Author: Yang Hongyang <yanghy@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_NET_FILTER_H
#define QEMU_NET_FILTER_H

#include "qapi/qapi-types-common.h"
#include "qemu/queue.h"
#include "qom/object.h"
#include "net/queue.h"

#define TYPE_NETFILTER "netfilter"
OBJECT_DECLARE_TYPE(NetFilterState, NetFilterClass, NETFILTER)

typedef void (FilterSetup) (NetFilterState *nf, Error **errp);
typedef void (FilterCleanup) (NetFilterState *nf);
typedef ssize_t (FilterReceiveIOV)(NetFilterState *nc,
                                   NetClientState *sender,
                                   unsigned flags,
                                   const struct iovec *iov,
                                   int iovcnt,
                                   NetPacketSent *sent_cb);

typedef void (FilterStatusChanged) (NetFilterState *nf, Error **errp);

typedef void (FilterHandleEvent) (NetFilterState *nf, int event, Error **errp);

struct NetFilterClass {
    ObjectClass parent_class;

    FilterSetup *setup;
    FilterCleanup *cleanup;
    FilterStatusChanged *status_changed;
    FilterHandleEvent *handle_event;
    FilterReceiveIOV *receive_iov;
};


struct NetFilterState {
    Object parent;

    char *netdev_id;
    NetClientState *netdev;
    NetFilterDirection direction;
    bool on;
    char *position;
    bool insert_before_flag;
    QTAILQ_ENTRY(NetFilterState) next;
};

ssize_t qemu_netfilter_receive(NetFilterState *nf,
                               NetFilterDirection direction,
                               NetClientState *sender,
                               unsigned flags,
                               const struct iovec *iov,
                               int iovcnt,
                               NetPacketSent *sent_cb);

ssize_t qemu_netfilter_pass_to_next(NetClientState *sender,
                                    unsigned flags,
                                    const struct iovec *iov,
                                    int iovcnt,
                                    void *opaque);

void colo_notify_filters_event(int event, Error **errp);

#endif
