/*
 * QEMU Crypto PBKDF support (Password-Based Key Derivation Function)
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#ifndef QCRYPTO_PBKDF_H
#define QCRYPTO_PBKDF_H

#include "crypto/hash.h"

bool qcrypto_pbkdf2_supports(QCryptoHashAlgo hash);


int qcrypto_pbkdf2(QCryptoHashAlgo hash,
                   const uint8_t *key, size_t nkey,
                   const uint8_t *salt, size_t nsalt,
                   uint64_t iterations,
                   uint8_t *out, size_t nout,
                   Error **errp);

uint64_t qcrypto_pbkdf2_count_iters(QCryptoHashAlgo hash,
                                    const uint8_t *key, size_t nkey,
                                    const uint8_t *salt, size_t nsalt,
                                    size_t nout,
                                    Error **errp);

#endif
