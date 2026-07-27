/*
 * Copyright (c) 2022 Bytedance
 * Author: lei he <helei.sig11@bytedance.com>
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

#ifndef QCRYPTO_ASN1_DECODER_H
#define QCRYPTO_ASN1_DECODER_H

#include "qapi/error.h"

typedef struct QCryptoEncodeContext QCryptoEncodeContext;

#define QCRYPTO_OID_rsaEncryption "\x2A\x86\x48\x86\xF7\x0D\x01\x01\x01"

typedef int (*QCryptoDERDecodeCb) (void *opaque, const uint8_t *value,
                                   size_t vlen, Error **errp);

int qcrypto_der_decode_int(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);
int qcrypto_der_decode_seq(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

int qcrypto_der_decode_oid(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

int qcrypto_der_decode_octet_str(const uint8_t **data,
                                 size_t *dlen,
                                 QCryptoDERDecodeCb cb,
                                 void *opaque,
                                 Error **errp);

int qcrypto_der_decode_bit_str(const uint8_t **data,
                               size_t *dlen,
                               QCryptoDERDecodeCb cb,
                               void *opaque,
                               Error **errp);


int qcrypto_der_decode_ctx_tag(const uint8_t **data,
                               size_t *dlen, int tag_id,
                               QCryptoDERDecodeCb cb,
                               void *opaque,
                               Error **errp);

QCryptoEncodeContext *qcrypto_der_encode_ctx_new(void);

void qcrypto_der_encode_seq_begin(QCryptoEncodeContext *ctx);

void qcrypto_der_encode_seq_end(QCryptoEncodeContext *ctx);


void qcrypto_der_encode_oid(QCryptoEncodeContext *ctx,
                            const uint8_t *src, size_t src_len);

void qcrypto_der_encode_int(QCryptoEncodeContext *ctx,
                            const uint8_t *src, size_t src_len);

void qcrypto_der_encode_null(QCryptoEncodeContext *ctx);

void qcrypto_der_encode_octet_str(QCryptoEncodeContext *ctx,
                                  const uint8_t *src, size_t src_len);

size_t qcrypto_der_encode_ctx_buffer_len(QCryptoEncodeContext *ctx);

void qcrypto_der_encode_ctx_flush_and_free(QCryptoEncodeContext *ctx,
                                           uint8_t *dst);

#endif
