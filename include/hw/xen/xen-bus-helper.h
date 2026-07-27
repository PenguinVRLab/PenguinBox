/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BUS_HELPER_H
#define HW_XEN_BUS_HELPER_H

#include "hw/xen/xen_backend_ops.h"

const char *xs_strstate(enum xenbus_state state);

void xs_node_create(struct qemu_xs_handle *h,  xs_transaction_t tid,
                    const char *node, unsigned int owner, unsigned int domid,
                    unsigned int perms, Error **errp);
void xs_node_destroy(struct qemu_xs_handle *h,  xs_transaction_t tid,
                     const char *node, Error **errp);

void xs_node_vprintf(struct qemu_xs_handle *h,  xs_transaction_t tid,
                     const char *node, const char *key, Error **errp,
                     const char *fmt, va_list ap)
    G_GNUC_PRINTF(6, 0);
void xs_node_printf(struct qemu_xs_handle *h,  xs_transaction_t tid,
                    const char *node, const char *key, Error **errp,
                    const char *fmt, ...)
    G_GNUC_PRINTF(6, 7);

int xs_node_vscanf(struct qemu_xs_handle *h,  xs_transaction_t tid,
                   const char *node, const char *key, Error **errp,
                   const char *fmt, va_list ap)
    G_GNUC_SCANF(6, 0);
int xs_node_scanf(struct qemu_xs_handle *h,  xs_transaction_t tid,
                  const char *node, const char *key, Error **errp,
                  const char *fmt, ...)
    G_GNUC_SCANF(6, 7);

char *xs_node_read(struct qemu_xs_handle *h, xs_transaction_t tid,
                   unsigned int *len, Error **errp,
                   const char *path_fmt, ...)
    G_GNUC_PRINTF(5, 6);

struct qemu_xs_watch *xs_node_watch(struct qemu_xs_handle *h, const char *node,
                                    const char *key, xs_watch_fn fn,
                                    void *opaque, Error **errp);
void xs_node_unwatch(struct qemu_xs_handle *h, struct qemu_xs_watch *w);

#endif
