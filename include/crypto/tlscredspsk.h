/*
 * QEMU crypto TLS Pre-Shared Key (PSK) support
 *
 * Copyright (c) 2018 Red Hat, Inc.
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

#ifndef QCRYPTO_TLSCREDSPSK_H
#define QCRYPTO_TLSCREDSPSK_H

#include "crypto/tlscreds.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_TLS_CREDS_PSK "tls-creds-psk"
typedef struct QCryptoTLSCredsPSK QCryptoTLSCredsPSK;
DECLARE_INSTANCE_CHECKER(QCryptoTLSCredsPSK, QCRYPTO_TLS_CREDS_PSK,
                         TYPE_QCRYPTO_TLS_CREDS_PSK)

typedef struct QCryptoTLSCredsPSKClass QCryptoTLSCredsPSKClass;

#define QCRYPTO_TLS_CREDS_PSKFILE "keys.psk"

struct QCryptoTLSCredsPSKClass {
    QCryptoTLSCredsClass parent_class;
};


#endif
