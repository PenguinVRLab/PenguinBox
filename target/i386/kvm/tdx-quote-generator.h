/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_I386_TDX_QUOTE_GENERATOR_H
#define QEMU_I386_TDX_QUOTE_GENERATOR_H

#include "qom/object_interfaces.h"
#include "io/channel-socket.h"
#include "exec/hwaddr.h"

#define TDX_GET_QUOTE_STRUCTURE_VERSION         1ULL

#define TDX_VP_GET_QUOTE_SUCCESS                0ULL
#define TDX_VP_GET_QUOTE_IN_FLIGHT              (-1ULL)
#define TDX_VP_GET_QUOTE_ERROR                  0x8000000000000000ULL
#define TDX_VP_GET_QUOTE_QGS_UNAVAILABLE        0x8000000000000001ULL

#define TDX_GET_QUOTE_MAX_BUF_LEN       (128 * 1024)
#define TDX_MAX_GET_QUOTE_REQUEST       16

#define TDX_GET_QUOTE_HDR_SIZE          24

struct tdx_get_quote_header {
    uint64_t structure_version;

    uint64_t error_code;

    uint32_t in_len;

    uint32_t out_len;

};

typedef struct TdxGenerateQuoteTask {
    hwaddr buf_gpa;
    hwaddr payload_gpa;
    uint64_t payload_len;

    char *send_data;
    uint64_t send_data_size;
    uint64_t send_data_sent;

    char *receive_buf;
    uint64_t receive_buf_received;

    uint64_t status_code;
    struct tdx_get_quote_header hdr;

    QIOChannelSocket *sioc;
    guint watch;
    QEMUTimer timer;

    void (*completion)(struct TdxGenerateQuoteTask *task);
    void *opaque;
} TdxGenerateQuoteTask;

void tdx_generate_quote(TdxGenerateQuoteTask *task, SocketAddress *qg_sock_addr);

#endif
