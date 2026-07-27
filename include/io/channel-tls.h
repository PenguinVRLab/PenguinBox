/*
 * QEMU I/O channels TLS driver
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#ifndef QIO_CHANNEL_TLS_H
#define QIO_CHANNEL_TLS_H

#include "io/channel.h"
#include "io/task.h"
#include "crypto/tlssession.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_TLS "qio-channel-tls"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelTLS, QIO_CHANNEL_TLS)


struct QIOChannelTLS {
    QIOChannel parent;
    QIOChannel *master;
    QCryptoTLSSession *session;
    QIOChannelShutdown shutdown;
    guint hs_ioc_tag;
    guint bye_ioc_tag;
};

void qio_channel_tls_bye(QIOChannelTLS *ioc, Error **errp);

QIOChannelTLS *
qio_channel_tls_new_server(QIOChannel *master,
                           QCryptoTLSCreds *creds,
                           const char *aclname,
                           Error **errp);

QIOChannelTLS *
qio_channel_tls_new_client(QIOChannel *master,
                           QCryptoTLSCreds *creds,
                           const char *hostname,
                           Error **errp);

void qio_channel_tls_handshake(QIOChannelTLS *ioc,
                               QIOTaskFunc func,
                               gpointer opaque,
                               GDestroyNotify destroy,
                               GMainContext *context);

QCryptoTLSSession *
qio_channel_tls_get_session(QIOChannelTLS *ioc);

#endif
