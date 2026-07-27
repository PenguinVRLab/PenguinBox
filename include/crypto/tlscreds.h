/*
 * QEMU crypto TLS credential support
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

#ifndef QCRYPTO_TLSCREDS_H
#define QCRYPTO_TLSCREDS_H

#include "qapi/qapi-types-crypto.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_TLS_CREDS "tls-creds"
typedef struct QCryptoTLSCreds QCryptoTLSCreds;
typedef struct QCryptoTLSCredsClass QCryptoTLSCredsClass;
DECLARE_OBJ_CHECKERS(QCryptoTLSCreds, QCryptoTLSCredsClass, QCRYPTO_TLS_CREDS,
                     TYPE_QCRYPTO_TLS_CREDS)


#define QCRYPTO_TLS_CREDS_DH_PARAMS "dh-params.pem"


typedef bool (*CryptoTLSCredsReload)(QCryptoTLSCreds *, Error **);

struct QCryptoTLSCredsClass {
    ObjectClass parent_class;
    CryptoTLSCredsReload reload;
    const char *prioritySuffix;
};

bool qcrypto_tls_creds_check_endpoint(QCryptoTLSCreds *creds,
                                      QCryptoTLSCredsEndpoint endpoint,
                                      Error **errp);


char *qcrypto_tls_creds_get_priority(QCryptoTLSCreds *creds);


bool qcrypto_tls_creds_reload(QCryptoTLSCreds *creds,
                              Error **errp);

#endif
