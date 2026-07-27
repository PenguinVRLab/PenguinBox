/*
 * QEMU DNS resolver
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_DNS_RESOLVER_H
#define QIO_DNS_RESOLVER_H

#include "qapi/qapi-types-sockets.h"
#include "qom/object.h"
#include "io/task.h"

#define TYPE_QIO_DNS_RESOLVER "qio-dns-resolver"
OBJECT_DECLARE_SIMPLE_TYPE(QIODNSResolver,
                           QIO_DNS_RESOLVER)


struct QIODNSResolver {
    Object parent;
};



QIODNSResolver *qio_dns_resolver_get_instance(void);

int qio_dns_resolver_lookup_sync(QIODNSResolver *resolver,
                                 SocketAddress *addr,
                                 size_t *naddrs,
                                 SocketAddress ***addrs,
                                 Error **errp);

void qio_dns_resolver_lookup_async(QIODNSResolver *resolver,
                                   SocketAddress *addr,
                                   QIOTaskFunc func,
                                   gpointer opaque,
                                   GDestroyNotify notify);

void qio_dns_resolver_lookup_result(QIODNSResolver *resolver,
                                    QIOTask *task,
                                    size_t *naddrs,
                                    SocketAddress ***addrs);

#endif
