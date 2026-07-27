/*
 * QEMU crypto TLS anonymous credential support
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

#ifndef QCRYPTO_TLSCREDSANON_H
#define QCRYPTO_TLSCREDSANON_H

#include "crypto/tlscreds.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_TLS_CREDS_ANON "tls-creds-anon"
typedef struct QCryptoTLSCredsAnon QCryptoTLSCredsAnon;
DECLARE_INSTANCE_CHECKER(QCryptoTLSCredsAnon, QCRYPTO_TLS_CREDS_ANON,
                         TYPE_QCRYPTO_TLS_CREDS_ANON)


typedef struct QCryptoTLSCredsAnonClass QCryptoTLSCredsAnonClass;

struct QCryptoTLSCredsAnonClass {
    QCryptoTLSCredsClass parent_class;
};


#endif
