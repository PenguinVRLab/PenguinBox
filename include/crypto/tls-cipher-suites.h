/*
 * QEMU TLS Cipher Suites Registry (RFC8447)
 *
 * Copyright (c) 2018-2020 Red Hat, Inc.
 *
 * Author: Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QCRYPTO_TLS_CIPHER_SUITES_H
#define QCRYPTO_TLS_CIPHER_SUITES_H

#include "qom/object.h"
#include "crypto/tlscreds.h"

#define TYPE_QCRYPTO_TLS_CIPHER_SUITES "tls-cipher-suites"
typedef struct QCryptoTLSCipherSuites QCryptoTLSCipherSuites;
DECLARE_INSTANCE_CHECKER(QCryptoTLSCipherSuites, QCRYPTO_TLS_CIPHER_SUITES,
                         TYPE_QCRYPTO_TLS_CIPHER_SUITES)

GByteArray *qcrypto_tls_cipher_suites_get_data(QCryptoTLSCipherSuites *obj,
                                               Error **errp);

#endif
