/*
 * QEMU Block backends
 *
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_BACKEND_COMMON_H
#define BLOCK_BACKEND_COMMON_H

#include "qemu/iov.h"
#include "block/throttle-groups.h"

#include "block/block.h"

typedef struct BlockDevOps {

    void (*change_media_cb)(void *opaque, bool load, Error **errp);
    void (*eject_request_cb)(void *opaque, bool force);

    bool (*is_medium_locked)(void *opaque);

    void (*drained_begin)(void *opaque);
    void (*drained_end)(void *opaque);
    bool (*drained_poll)(void *opaque);

    bool (*is_tray_open)(void *opaque);

    void (*resize_cb)(void *opaque);
} BlockDevOps;

typedef struct BlockBackendPublic {
    ThrottleGroupMember throttle_group_member;
} BlockBackendPublic;

#endif
