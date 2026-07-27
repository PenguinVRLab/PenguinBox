/*
 * QEMU Crypto Device Implementation
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
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
#ifndef CRYPTODEV_H
#define CRYPTODEV_H

#include "qemu/queue.h"
#include "qemu/throttle.h"
#include "qom/object.h"
#include "qapi/qapi-types-cryptodev.h"

#define TYPE_CRYPTODEV_BACKEND "cryptodev-backend"

OBJECT_DECLARE_TYPE(CryptoDevBackend, CryptoDevBackendClass,
                    CRYPTODEV_BACKEND)


#define MAX_CRYPTO_QUEUE_NUM  64

typedef struct CryptoDevBackendConf CryptoDevBackendConf;
typedef struct CryptoDevBackendPeers CryptoDevBackendPeers;
typedef struct CryptoDevBackendClient
                     CryptoDevBackendClient;

typedef struct CryptoDevBackendSymSessionInfo {
    uint32_t cipher_alg;
    uint32_t key_len;
    uint32_t hash_alg;
    uint32_t hash_result_len;
    uint32_t auth_key_len;
    uint32_t add_len;
    uint8_t op_type;
    uint8_t direction;
    uint8_t hash_mode;
    uint8_t alg_chain_order;
    uint8_t *cipher_key;
    uint8_t *auth_key;
} CryptoDevBackendSymSessionInfo;

typedef struct CryptoDevBackendRsaPara {
    uint32_t padding_algo;
    uint32_t hash_algo;
} CryptoDevBackendRsaPara;

typedef struct CryptoDevBackendAsymSessionInfo {
    uint32_t algo;
    uint32_t keytype;
    uint32_t keylen;
    uint8_t *key;
    union {
        CryptoDevBackendRsaPara rsa;
    } u;
} CryptoDevBackendAsymSessionInfo;

typedef struct CryptoDevBackendSessionInfo {
    uint32_t op_code;
    union {
        CryptoDevBackendSymSessionInfo sym_sess_info;
        CryptoDevBackendAsymSessionInfo asym_sess_info;
    } u;
    uint64_t session_id;
} CryptoDevBackendSessionInfo;

typedef struct CryptoDevBackendSymOpInfo {
    uint32_t aad_len;
    uint32_t iv_len;
    uint32_t src_len;
    uint32_t dst_len;
    uint32_t digest_result_len;
    uint32_t hash_start_src_offset;
    uint32_t cipher_start_src_offset;
    uint32_t len_to_hash;
    uint32_t len_to_cipher;
    uint8_t op_type;
    uint8_t *iv;
    uint8_t *src;
    uint8_t *dst;
    uint8_t *aad_data;
    uint8_t *digest_result;
    uint8_t data[];
} CryptoDevBackendSymOpInfo;


typedef struct CryptoDevBackendAsymOpInfo {
    uint32_t src_len;
    uint32_t dst_len;
    uint8_t *src;
    uint8_t *dst;
} CryptoDevBackendAsymOpInfo;

typedef void (*CryptoDevCompletionFunc) (void *opaque, int ret);

typedef struct CryptoDevBackendOpInfo {
    QCryptodevBackendAlgoType algtype;
    uint32_t op_code;
    uint32_t queue_index;
    CryptoDevCompletionFunc cb;
    void *opaque;
    uint64_t session_id;
    union {
        CryptoDevBackendSymOpInfo *sym_op_info;
        CryptoDevBackendAsymOpInfo *asym_op_info;
    } u;
    QTAILQ_ENTRY(CryptoDevBackendOpInfo) next;
} CryptoDevBackendOpInfo;

struct CryptoDevBackendClass {
    ObjectClass parent_class;

    void (*init)(CryptoDevBackend *backend, Error **errp);
    void (*cleanup)(CryptoDevBackend *backend, Error **errp);

    int (*create_session)(CryptoDevBackend *backend,
                          CryptoDevBackendSessionInfo *sess_info,
                          uint32_t queue_index,
                          CryptoDevCompletionFunc cb,
                          void *opaque);

    int (*close_session)(CryptoDevBackend *backend,
                         uint64_t session_id,
                         uint32_t queue_index,
                         CryptoDevCompletionFunc cb,
                         void *opaque);

    int (*do_op)(CryptoDevBackend *backend,
                 CryptoDevBackendOpInfo *op_info);
};

struct CryptoDevBackendClient {
    QCryptodevBackendType type;
    char *info_str;
    unsigned int queue_index;
    int vring_enable;
    QTAILQ_ENTRY(CryptoDevBackendClient) next;
};

struct CryptoDevBackendPeers {
    CryptoDevBackendClient *ccs[MAX_CRYPTO_QUEUE_NUM];
    uint32_t queues;
};

struct CryptoDevBackendConf {
    CryptoDevBackendPeers peers;

    uint32_t crypto_services;

    uint32_t cipher_algo_l;
    uint32_t cipher_algo_h;
    uint32_t hash_algo;
    uint32_t mac_algo_l;
    uint32_t mac_algo_h;
    uint32_t aead_algo;
    uint32_t akcipher_algo;
    uint32_t max_cipher_key_len;
    uint32_t max_auth_key_len;
    uint64_t max_size;
};

typedef struct CryptodevBackendSymStat {
    int64_t encrypt_ops;
    int64_t decrypt_ops;
    int64_t encrypt_bytes;
    int64_t decrypt_bytes;
} CryptodevBackendSymStat;

typedef struct CryptodevBackendAsymStat {
    int64_t encrypt_ops;
    int64_t decrypt_ops;
    int64_t sign_ops;
    int64_t verify_ops;
    int64_t encrypt_bytes;
    int64_t decrypt_bytes;
    int64_t sign_bytes;
    int64_t verify_bytes;
} CryptodevBackendAsymStat;

struct CryptoDevBackend {
    Object parent_obj;

    bool ready;
    bool is_used;
    CryptoDevBackendConf conf;
    CryptodevBackendSymStat *sym_stat;
    CryptodevBackendAsymStat *asym_stat;

    ThrottleState ts;
    ThrottleTimers tt;
    ThrottleConfig tc;
    QTAILQ_HEAD(, CryptoDevBackendOpInfo) opinfos;
};

#define CryptodevSymStatInc(be, op, bytes) do { \
   be->sym_stat->op##_bytes += (bytes); \
   be->sym_stat->op##_ops += 1; \
} while ( 0)

#define CryptodevSymStatIncEncrypt(be, bytes) \
            CryptodevSymStatInc(be, encrypt, bytes)

#define CryptodevSymStatIncDecrypt(be, bytes) \
            CryptodevSymStatInc(be, decrypt, bytes)

#define CryptodevAsymStatInc(be, op, bytes) do { \
    be->asym_stat->op##_bytes += (bytes); \
    be->asym_stat->op##_ops += 1; \
} while ( 0)

#define CryptodevAsymStatIncEncrypt(be, bytes) \
            CryptodevAsymStatInc(be, encrypt, bytes)

#define CryptodevAsymStatIncDecrypt(be, bytes) \
            CryptodevAsymStatInc(be, decrypt, bytes)

#define CryptodevAsymStatIncSign(be, bytes) \
            CryptodevAsymStatInc(be, sign, bytes)

#define CryptodevAsymStatIncVerify(be, bytes) \
            CryptodevAsymStatInc(be, verify, bytes)


CryptoDevBackendClient *cryptodev_backend_new_client(void);

void cryptodev_backend_free_client(
                  CryptoDevBackendClient *cc);

void cryptodev_backend_cleanup(
           CryptoDevBackend *backend,
           Error **errp);

int cryptodev_backend_create_session(
           CryptoDevBackend *backend,
           CryptoDevBackendSessionInfo *sess_info,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque);

int cryptodev_backend_close_session(
           CryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque);

int cryptodev_backend_crypto_operation(
                 CryptoDevBackend *backend,
                 CryptoDevBackendOpInfo *op_info);

void cryptodev_backend_set_used(CryptoDevBackend *backend, bool used);

bool cryptodev_backend_is_used(CryptoDevBackend *backend);

void cryptodev_backend_set_ready(CryptoDevBackend *backend, bool ready);

bool cryptodev_backend_is_ready(CryptoDevBackend *backend);

#endif
