/*
 * QEMU crypto TLS x509 credential support
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

#ifndef QCRYPTO_TLSCREDSX509_H
#define QCRYPTO_TLSCREDSX509_H

#include "crypto/tlscreds.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_TLS_CREDS_X509 "tls-creds-x509"
typedef struct QCryptoTLSCredsX509 QCryptoTLSCredsX509;
DECLARE_INSTANCE_CHECKER(QCryptoTLSCredsX509, QCRYPTO_TLS_CREDS_X509,
                         TYPE_QCRYPTO_TLS_CREDS_X509)

typedef struct QCryptoTLSCredsX509Class QCryptoTLSCredsX509Class;

#define QCRYPTO_TLS_CREDS_X509_CA_CERT "ca-cert.pem"
#define QCRYPTO_TLS_CREDS_X509_CA_CRL "ca-crl.pem"
#define QCRYPTO_TLS_CREDS_X509_SERVER_KEY "server-key.pem"
#define QCRYPTO_TLS_CREDS_X509_SERVER_CERT "server-cert.pem"
#define QCRYPTO_TLS_CREDS_X509_CLIENT_KEY "client-key.pem"
#define QCRYPTO_TLS_CREDS_X509_CLIENT_CERT "client-cert.pem"
#define QCRYPTO_TLS_CREDS_X509_SERVER_KEY_N "server-key-%zu.pem"
#define QCRYPTO_TLS_CREDS_X509_SERVER_CERT_N "server-cert-%zu.pem"
#define QCRYPTO_TLS_CREDS_X509_CLIENT_KEY_N "client-key-%zu.pem"
#define QCRYPTO_TLS_CREDS_X509_CLIENT_CERT_N "client-cert-%zu.pem"

#define QCRYPTO_TLS_CREDS_X509_IDENTITY_MAX 4

struct QCryptoTLSCredsX509Class {
    QCryptoTLSCredsClass parent_class;
};


#endif
