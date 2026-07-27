/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
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

#ifndef QCRYPTO_HASH_H
#define QCRYPTO_HASH_H

#include "qapi/qapi-types-crypto.h"

#define QCRYPTO_HASH_DIGEST_LEN_MD5       16
#define QCRYPTO_HASH_DIGEST_LEN_SHA1      20
#define QCRYPTO_HASH_DIGEST_LEN_SHA224    28
#define QCRYPTO_HASH_DIGEST_LEN_SHA256    32
#define QCRYPTO_HASH_DIGEST_LEN_SHA384    48
#define QCRYPTO_HASH_DIGEST_LEN_SHA512    64
#define QCRYPTO_HASH_DIGEST_LEN_RIPEMD160 20
#define QCRYPTO_HASH_DIGEST_LEN_SM3       32

typedef struct QCryptoHash QCryptoHash;
struct QCryptoHash {
    QCryptoHashAlgo alg;
    void *opaque;
    void *driver;
};

gboolean qcrypto_hash_supports(QCryptoHashAlgo alg);


size_t qcrypto_hash_digest_len(QCryptoHashAlgo alg);

int qcrypto_hash_bytesv(QCryptoHashAlgo alg,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp);

int qcrypto_hash_bytes(QCryptoHashAlgo alg,
                       const void *buf,
                       size_t len,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp);

int qcrypto_hash_digestv(QCryptoHashAlgo alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **digest,
                         Error **errp);

int qcrypto_hash_updatev(QCryptoHash *hash,
                         const struct iovec *iov,
                         size_t niov,
                         Error **errp);
int qcrypto_hash_update(QCryptoHash *hash,
                        const void *buf,
                        size_t len,
                        Error **errp);

int qcrypto_hash_finalize_digest(QCryptoHash *hash,
                                 char **digest,
                                 Error **errp);

int qcrypto_hash_finalize_base64(QCryptoHash *hash,
                                 char **base64,
                                 Error **errp);

int qcrypto_hash_finalize_bytes(QCryptoHash *hash,
                                uint8_t **result,
                                size_t *result_len,
                                Error **errp);

QCryptoHash *qcrypto_hash_new(QCryptoHashAlgo alg, Error **errp);

void qcrypto_hash_free(QCryptoHash *hash);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoHash, qcrypto_hash_free)

int qcrypto_hash_digest(QCryptoHashAlgo alg,
                        const void *buf,
                        size_t len,
                        char **digest,
                        Error **errp);

int qcrypto_hash_base64v(QCryptoHashAlgo alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **base64,
                         Error **errp);

int qcrypto_hash_base64(QCryptoHashAlgo alg,
                        const void *buf,
                        size_t len,
                        char **base64,
                        Error **errp);

#endif
