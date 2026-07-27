/*
 * QEMU crypto TLS session support
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

#ifndef QCRYPTO_TLSSESSION_H
#define QCRYPTO_TLSSESSION_H

#include "crypto/tlscreds.h"

typedef struct QCryptoTLSSession QCryptoTLSSession;

#define QCRYPTO_TLS_SESSION_ERR_BLOCK -2
#define QCRYPTO_TLS_SESSION_PREMATURE_TERMINATION -3

QCryptoTLSSession *qcrypto_tls_session_new(QCryptoTLSCreds *creds,
                                           const char *hostname,
                                           const char *aclname,
                                           QCryptoTLSCredsEndpoint endpoint,
                                           Error **errp);

void qcrypto_tls_session_free(QCryptoTLSSession *sess);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoTLSSession, qcrypto_tls_session_free)

void qcrypto_tls_session_require_thread_safety(QCryptoTLSSession *sess);

int qcrypto_tls_session_check_credentials(QCryptoTLSSession *sess,
                                          Error **errp);

typedef ssize_t (*QCryptoTLSSessionWriteFunc)(const void *buf,
                                              size_t len,
                                              void *opaque,
                                              Error **errp);
typedef ssize_t (*QCryptoTLSSessionReadFunc)(void *buf,
                                             size_t len,
                                             void *opaque,
                                             Error **errp);

void qcrypto_tls_session_set_callbacks(QCryptoTLSSession *sess,
                                       QCryptoTLSSessionWriteFunc writeFunc,
                                       QCryptoTLSSessionReadFunc readFunc,
                                       void *opaque);

ssize_t qcrypto_tls_session_write(QCryptoTLSSession *sess,
                                  const char *buf,
                                  size_t len,
                                  Error **errp);

ssize_t qcrypto_tls_session_read(QCryptoTLSSession *sess,
                                 char *buf,
                                 size_t len,
                                 Error **errp);

size_t qcrypto_tls_session_check_pending(QCryptoTLSSession *sess);

int qcrypto_tls_session_handshake(QCryptoTLSSession *sess,
                                  Error **errp);

typedef enum {
    QCRYPTO_TLS_HANDSHAKE_COMPLETE,
    QCRYPTO_TLS_HANDSHAKE_SENDING,
    QCRYPTO_TLS_HANDSHAKE_RECVING,
} QCryptoTLSSessionHandshakeStatus;

typedef enum {
    QCRYPTO_TLS_BYE_COMPLETE,
    QCRYPTO_TLS_BYE_SENDING,
    QCRYPTO_TLS_BYE_RECVING,
} QCryptoTLSSessionByeStatus;

int
qcrypto_tls_session_bye(QCryptoTLSSession *session, Error **errp);

int qcrypto_tls_session_get_key_size(QCryptoTLSSession *sess,
                                     Error **errp);

char *qcrypto_tls_session_get_peer_name(QCryptoTLSSession *sess);

#endif
