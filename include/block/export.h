/*
 * Declarations for block exports
 *
 * Copyright (c) 2012, 2020 Red Hat, Inc.
 *
 * Authors:
 * Paolo Bonzini <pbonzini@redhat.com>
 * Kevin Wolf <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef BLOCK_EXPORT_H
#define BLOCK_EXPORT_H

#include "qapi/qapi-types-block-export.h"
#include "qemu/queue.h"

typedef struct BlockExport BlockExport;

typedef struct BlockExportDriver {
    BlockExportType type;

    size_t instance_size;

    bool supports_inactive;

    int (*create)(BlockExport *, BlockExportOptions *, Error **);

    void (*delete)(BlockExport *);

    void (*request_shutdown)(BlockExport *);
} BlockExportDriver;

struct BlockExport {
    const BlockExportDriver *drv;

    char *id;

    int refcount;

    bool user_owned;

    AioContext *ctx;

    BlockBackend *blk;

    QLIST_ENTRY(BlockExport) next;
};

BlockExport *blk_exp_add(BlockExportOptions *export, Error **errp);
BlockExport *blk_exp_find(const char *id);
void blk_exp_ref(BlockExport *exp);
void blk_exp_unref(BlockExport *exp);
void blk_exp_request_shutdown(BlockExport *exp);
void blk_exp_close_all(void);
void blk_exp_close_all_type(BlockExportType type);

#endif
