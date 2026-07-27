/*
 * QEMU Crypto asymmetric algorithms
 *
 * Copyright (c) 2022 Bytedance
 * Author: zhenwei pi <pizhenwei@bytedance.com>
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

#ifndef QCRYPTO_AKCIPHER_H
#define QCRYPTO_AKCIPHER_H

#include "qapi/qapi-types-crypto.h"

typedef struct QCryptoAkCipher QCryptoAkCipher;

bool qcrypto_akcipher_supports(QCryptoAkCipherOptions *opts);

QCryptoAkCipher *qcrypto_akcipher_new(const QCryptoAkCipherOptions *opts,
                                      QCryptoAkCipherKeyType type,
                                      const uint8_t *key, size_t key_len,
                                      Error **errp);

int qcrypto_akcipher_encrypt(QCryptoAkCipher *akcipher,
                             const void *in, size_t in_len,
                             void *out, size_t out_len, Error **errp);

int qcrypto_akcipher_decrypt(QCryptoAkCipher *akcipher,
                             const void *in, size_t in_len,
                             void *out, size_t out_len, Error **errp);

int qcrypto_akcipher_sign(QCryptoAkCipher *akcipher,
                          const void *in, size_t in_len,
                          void *out, size_t out_len, Error **errp);

int qcrypto_akcipher_verify(QCryptoAkCipher *akcipher,
                            const void *in, size_t in_len,
                            const void *in2, size_t in2_len, Error **errp);

int qcrypto_akcipher_max_plaintext_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_max_ciphertext_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_max_signature_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_max_dgst_len(QCryptoAkCipher *akcipher);

void qcrypto_akcipher_free(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_export_p8info(const QCryptoAkCipherOptions *opts,
                                   uint8_t *key, size_t keylen,
                                   uint8_t **dst, size_t *dst_len,
                                   Error **errp);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoAkCipher, qcrypto_akcipher_free)

#endif
