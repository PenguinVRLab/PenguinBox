/*
 *  Copyright Red Hat
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NBD_H
#define NBD_H

#include "block/export.h"
#include "io/channel-socket.h"
#include "crypto/tlscreds.h"
#include "qapi/error.h"
#include "qemu/bswap.h"

typedef struct NBDExport NBDExport;
typedef struct NBDClient NBDClient;
typedef struct NBDClientConnection NBDClientConnection;
typedef struct NBDMetaContexts NBDMetaContexts;

extern const BlockExportDriver blk_exp_nbd;

#define NBD_DEFAULT_HANDSHAKE_MAX_SECS 10

#define NBD_DEFAULT_MAX_CONNECTIONS 100

typedef struct NBDOption {
    uint64_t magic;
    uint32_t option;
    uint32_t length;
} QEMU_PACKED NBDOption;

typedef struct NBDOptionReply {
    uint64_t magic;
    uint32_t option;
    uint32_t type;
    uint32_t length;
} QEMU_PACKED NBDOptionReply;

typedef struct NBDOptionReplyMetaContext {
    NBDOptionReply h;
    uint32_t context_id;
} QEMU_PACKED NBDOptionReplyMetaContext;

typedef enum NBDMode {
    NBD_MODE_OLDSTYLE,
    NBD_MODE_EXPORT_NAME,
    NBD_MODE_SIMPLE,
    NBD_MODE_STRUCTURED,
    NBD_MODE_EXTENDED,
} NBDMode;

typedef struct NBDRequest {
    uint64_t cookie;
    uint64_t from;
    uint64_t len;
    uint16_t flags;
    uint16_t type;
    NBDMode mode;
    NBDMetaContexts *contexts;
} NBDRequest;

typedef struct NBDSimpleReply {
    uint32_t magic;
    uint32_t error;
    uint64_t cookie;
} QEMU_PACKED NBDSimpleReply;

typedef struct NBDStructuredReplyChunk {
    uint32_t magic;
    uint16_t flags;
    uint16_t type;
    uint64_t cookie;
    uint32_t length;
} QEMU_PACKED NBDStructuredReplyChunk;

typedef struct NBDExtendedReplyChunk {
    uint32_t magic;
    uint16_t flags;
    uint16_t type;
    uint64_t cookie;
    uint64_t offset;
    uint64_t length;
} QEMU_PACKED NBDExtendedReplyChunk;

typedef union NBDReply {
    NBDSimpleReply simple;
    NBDStructuredReplyChunk structured;
    NBDExtendedReplyChunk extended;
    struct {
        uint32_t magic;
        uint32_t _skip;
        uint64_t cookie;
    };
} NBDReply;
QEMU_BUILD_BUG_ON(offsetof(NBDReply, simple.cookie) !=
                  offsetof(NBDReply, cookie));
QEMU_BUILD_BUG_ON(offsetof(NBDReply, structured.cookie) !=
                  offsetof(NBDReply, cookie));
QEMU_BUILD_BUG_ON(offsetof(NBDReply, extended.cookie) !=
                  offsetof(NBDReply, cookie));

typedef struct NBDStructuredReadData {
    uint64_t offset;
} QEMU_PACKED NBDStructuredReadData;

typedef struct NBDStructuredReadHole {
    uint64_t offset;
    uint32_t length;
} QEMU_PACKED NBDStructuredReadHole;

typedef struct NBDStructuredError {
    uint32_t error;
    uint16_t message_length;
} QEMU_PACKED NBDStructuredError;

typedef struct NBDStructuredMeta {
    uint32_t context_id;
} QEMU_PACKED NBDStructuredMeta;

typedef struct NBDExtent32 {
    uint32_t length;
    uint32_t flags;
} QEMU_PACKED NBDExtent32;

typedef struct NBDExtendedMeta {
    uint32_t context_id;
    uint32_t count;
} QEMU_PACKED NBDExtendedMeta;

typedef struct NBDExtent64 {
    uint64_t length;
    uint64_t flags;
} QEMU_PACKED NBDExtent64;

typedef struct NBDBlockStatusPayload {
    uint64_t effect_length;
} QEMU_PACKED NBDBlockStatusPayload;

enum {
    NBD_FLAG_HAS_FLAGS_BIT          =  0,
    NBD_FLAG_READ_ONLY_BIT          =  1,
    NBD_FLAG_SEND_FLUSH_BIT         =  2,
    NBD_FLAG_SEND_FUA_BIT           =  3,
    NBD_FLAG_ROTATIONAL_BIT         =  4,
    NBD_FLAG_SEND_TRIM_BIT          =  5,
    NBD_FLAG_SEND_WRITE_ZEROES_BIT  =  6,
    NBD_FLAG_SEND_DF_BIT            =  7,
    NBD_FLAG_CAN_MULTI_CONN_BIT     =  8,
    NBD_FLAG_SEND_RESIZE_BIT        =  9,
    NBD_FLAG_SEND_CACHE_BIT         = 10,
    NBD_FLAG_SEND_FAST_ZERO_BIT     = 11,
    NBD_FLAG_BLOCK_STAT_PAYLOAD_BIT = 12,
};

#define NBD_FLAG_HAS_FLAGS          (1 << NBD_FLAG_HAS_FLAGS_BIT)
#define NBD_FLAG_READ_ONLY          (1 << NBD_FLAG_READ_ONLY_BIT)
#define NBD_FLAG_SEND_FLUSH         (1 << NBD_FLAG_SEND_FLUSH_BIT)
#define NBD_FLAG_SEND_FUA           (1 << NBD_FLAG_SEND_FUA_BIT)
#define NBD_FLAG_ROTATIONAL         (1 << NBD_FLAG_ROTATIONAL_BIT)
#define NBD_FLAG_SEND_TRIM          (1 << NBD_FLAG_SEND_TRIM_BIT)
#define NBD_FLAG_SEND_WRITE_ZEROES  (1 << NBD_FLAG_SEND_WRITE_ZEROES_BIT)
#define NBD_FLAG_SEND_DF            (1 << NBD_FLAG_SEND_DF_BIT)
#define NBD_FLAG_CAN_MULTI_CONN     (1 << NBD_FLAG_CAN_MULTI_CONN_BIT)
#define NBD_FLAG_SEND_RESIZE        (1 << NBD_FLAG_SEND_RESIZE_BIT)
#define NBD_FLAG_SEND_CACHE         (1 << NBD_FLAG_SEND_CACHE_BIT)
#define NBD_FLAG_SEND_FAST_ZERO     (1 << NBD_FLAG_SEND_FAST_ZERO_BIT)
#define NBD_FLAG_BLOCK_STAT_PAYLOAD (1 << NBD_FLAG_BLOCK_STAT_PAYLOAD_BIT)

#define NBD_FLAG_FIXED_NEWSTYLE   (1 << 0)
#define NBD_FLAG_NO_ZEROES        (1 << 1)

#define NBD_FLAG_C_FIXED_NEWSTYLE (1 << 0)
#define NBD_FLAG_C_NO_ZEROES      (1 << 1)

#define NBD_OPT_EXPORT_NAME       (1)
#define NBD_OPT_ABORT             (2)
#define NBD_OPT_LIST              (3)
#define NBD_OPT_STARTTLS          (5)
#define NBD_OPT_INFO              (6)
#define NBD_OPT_GO                (7)
#define NBD_OPT_STRUCTURED_REPLY  (8)
#define NBD_OPT_LIST_META_CONTEXT (9)
#define NBD_OPT_SET_META_CONTEXT  (10)
#define NBD_OPT_EXTENDED_HEADERS  (11)

#define NBD_REP_ERR(value) ((UINT32_C(1) << 31) | (value))

#define NBD_REP_ACK             (1)
#define NBD_REP_SERVER          (2)
#define NBD_REP_INFO            (3)
#define NBD_REP_META_CONTEXT    (4)

#define NBD_REP_ERR_UNSUP           NBD_REP_ERR(1)
#define NBD_REP_ERR_POLICY          NBD_REP_ERR(2)
#define NBD_REP_ERR_INVALID         NBD_REP_ERR(3)
#define NBD_REP_ERR_PLATFORM        NBD_REP_ERR(4)
#define NBD_REP_ERR_TLS_REQD        NBD_REP_ERR(5)
#define NBD_REP_ERR_UNKNOWN         NBD_REP_ERR(6)
#define NBD_REP_ERR_SHUTDOWN        NBD_REP_ERR(7)
#define NBD_REP_ERR_BLOCK_SIZE_REQD NBD_REP_ERR(8)
#define NBD_REP_ERR_TOO_BIG         NBD_REP_ERR(9)
#define NBD_REP_ERR_EXT_HEADER_REQD NBD_REP_ERR(10)

#define NBD_INFO_EXPORT         0
#define NBD_INFO_NAME           1
#define NBD_INFO_DESCRIPTION    2
#define NBD_INFO_BLOCK_SIZE     3

#define NBD_CMD_FLAG_FUA         (1 << 0)
#define NBD_CMD_FLAG_NO_HOLE     (1 << 1)
#define NBD_CMD_FLAG_DF          (1 << 2)
#define NBD_CMD_FLAG_REQ_ONE     (1 << 3) \

#define NBD_CMD_FLAG_FAST_ZERO   (1 << 4)
#define NBD_CMD_FLAG_PAYLOAD_LEN (1 << 5) \


enum {
    NBD_CMD_READ = 0,
    NBD_CMD_WRITE = 1,
    NBD_CMD_DISC = 2,
    NBD_CMD_FLUSH = 3,
    NBD_CMD_TRIM = 4,
    NBD_CMD_CACHE = 5,
    NBD_CMD_WRITE_ZEROES = 6,
    NBD_CMD_BLOCK_STATUS = 7,
};

#define NBD_DEFAULT_PORT 10809

#define NBD_MAX_BUFFER_SIZE (32 * 1024 * 1024)

#define NBD_MAX_STRING_SIZE 4096

#define NBD_REQUEST_MAGIC           0x25609513
#define NBD_EXTENDED_REQUEST_MAGIC  0x21e41c71

#define NBD_SIMPLE_REPLY_MAGIC      0x67446698
#define NBD_STRUCTURED_REPLY_MAGIC  0x668e33ef
#define NBD_EXTENDED_REPLY_MAGIC    0x6e8a278c

#define NBD_REPLY_FLAG_DONE          (1 << 0)

#define NBD_REPLY_ERR(value)         ((1 << 15) | (value))

#define NBD_REPLY_TYPE_NONE              0
#define NBD_REPLY_TYPE_OFFSET_DATA       1
#define NBD_REPLY_TYPE_OFFSET_HOLE       2
#define NBD_REPLY_TYPE_BLOCK_STATUS      5
#define NBD_REPLY_TYPE_BLOCK_STATUS_EXT  6
#define NBD_REPLY_TYPE_ERROR             NBD_REPLY_ERR(1)
#define NBD_REPLY_TYPE_ERROR_OFFSET      NBD_REPLY_ERR(2)

#define NBD_STATE_HOLE (1 << 0)
#define NBD_STATE_ZERO (1 << 1)

#define NBD_STATE_DIRTY (1 << 0)

static inline bool nbd_reply_type_is_error(int type)
{
    return type & (1 << 15);
}

#define NBD_SUCCESS    0
#define NBD_EPERM      1
#define NBD_EIO        5
#define NBD_ENOMEM     12
#define NBD_EINVAL     22
#define NBD_ENOSPC     28
#define NBD_EOVERFLOW  75
#define NBD_ENOTSUP    95
#define NBD_ESHUTDOWN  108

typedef struct NBDExportInfo {
    bool request_sizes;
    char *x_dirty_bitmap;

    char *name;

    NBDMode mode;
    bool base_allocation;

    uint64_t size;
    uint16_t flags;
    uint32_t min_block;
    uint32_t opt_block;
    uint32_t max_block;

    uint32_t context_id;

    char *description;
    int n_contexts;
    char **contexts;
} NBDExportInfo;

int nbd_receive_negotiate(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                          const char *hostname, QIOChannel **outioc,
                          NBDExportInfo *info, Error **errp);
void nbd_free_export_list(NBDExportInfo *info, int count);
int nbd_receive_export_list(QIOChannel *ioc, QCryptoTLSCreds *tlscreds,
                            const char *hostname, NBDExportInfo **info,
                            Error **errp);
int nbd_init(int fd, QIOChannelSocket *sioc, NBDExportInfo *info,
             Error **errp);
int nbd_send_request(QIOChannel *ioc, NBDRequest *request);
int coroutine_fn nbd_receive_reply(BlockDriverState *bs, QIOChannel *ioc,
                                   NBDReply *reply, NBDMode mode,
                                   Error **errp);
int nbd_client(int fd);
int nbd_disconnect(int fd);
int nbd_errno_to_system_errno(int err);

void nbd_export_set_on_eject_blk(BlockExport *exp, BlockBackend *blk);

AioContext *nbd_export_aio_context(NBDExport *exp);
NBDExport *nbd_export_find(const char *name);

void nbd_client_new(QIOChannelSocket *sioc,
                    uint32_t handshake_max_secs,
                    QCryptoTLSCreds *tlscreds,
                    const char *tlsauthz,
                    void (*close_fn)(NBDClient *, bool),
                    void *owner);
void *nbd_client_owner(NBDClient *client);
void nbd_client_get(NBDClient *client);
void nbd_client_put(NBDClient *client);

void nbd_server_is_qemu_nbd(int max_connections);
bool nbd_server_is_running(void);
int nbd_server_max_connections(void);
void nbd_server_start(SocketAddress *addr, uint32_t handshake_max_secs,
                      const char *tls_creds, const char *tls_authz,
                      uint32_t max_connections, Error **errp);
void nbd_server_start_options(NbdServerOptions *arg, Error **errp);

static inline int nbd_read(QIOChannel *ioc, void *buffer, size_t size,
                           const char *desc, Error **errp)
{
    ERRP_GUARD();
    int ret = qio_channel_read_all(ioc, buffer, size, errp) < 0 ? -EIO : 0;

    if (ret < 0) {
        if (desc) {
            error_prepend(errp, "Failed to read %s: ", desc);
        }
        return ret;
    }

    return 0;
}

#define DEF_NBD_READ_N(bits)                                            \
static inline int nbd_read##bits(QIOChannel *ioc,                       \
                                 uint##bits##_t *val,                   \
                                 const char *desc, Error **errp)        \
{                                                                       \
    int ret = nbd_read(ioc, val, sizeof(*val), desc, errp);             \
    if (ret < 0) {                                                      \
        return ret;                                                     \
    }                                                                   \
    *val = be##bits##_to_cpu(*val);                                     \
    return 0;                                                           \
}

DEF_NBD_READ_N(16)
DEF_NBD_READ_N(32)
DEF_NBD_READ_N(64)

#undef DEF_NBD_READ_N

static inline bool nbd_reply_is_simple(NBDReply *reply)
{
    return reply->magic == NBD_SIMPLE_REPLY_MAGIC;
}

static inline bool nbd_reply_is_structured(NBDReply *reply)
{
    return reply->magic == NBD_STRUCTURED_REPLY_MAGIC;
}

const char *nbd_reply_type_lookup(uint16_t type);
const char *nbd_opt_lookup(uint32_t opt);
const char *nbd_rep_lookup(uint32_t rep);
const char *nbd_info_lookup(uint16_t info);
const char *nbd_cmd_lookup(uint16_t info);
const char *nbd_err_lookup(int err);
const char *nbd_mode_lookup(NBDMode mode);

void nbd_client_connection_enable_retry(NBDClientConnection *conn);

NBDClientConnection *nbd_client_connection_new(const SocketAddress *saddr,
                                               bool do_negotiation,
                                               const char *export_name,
                                               const char *x_dirty_bitmap,
                                               QCryptoTLSCreds *tlscreds,
                                               const char *tlshostname);
void nbd_client_connection_release(NBDClientConnection *conn);

QIOChannel *coroutine_fn
nbd_co_establish_connection(NBDClientConnection *conn, NBDExportInfo *info,
                            bool blocking, Error **errp);

void nbd_co_establish_connection_cancel(NBDClientConnection *conn);

#endif
