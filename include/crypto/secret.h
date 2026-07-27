/*
 * QEMU crypto secret support
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

#ifndef QCRYPTO_SECRET_H
#define QCRYPTO_SECRET_H

#include "qapi/qapi-types-crypto.h"
#include "qom/object.h"
#include "crypto/secret_common.h"

#define TYPE_QCRYPTO_SECRET "secret"
typedef struct QCryptoSecret QCryptoSecret;
DECLARE_INSTANCE_CHECKER(QCryptoSecret, QCRYPTO_SECRET,
                         TYPE_QCRYPTO_SECRET)

typedef struct QCryptoSecretClass QCryptoSecretClass;

struct QCryptoSecret {
    QCryptoSecretCommon parent_obj;
    char *data;
    char *file;
};


struct QCryptoSecretClass {
    QCryptoSecretCommonClass parent_class;
};

#endif
