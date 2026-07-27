/*
 * Declarations for long-running block device operations
 *
 * Copyright (c) 2011 IBM Corp.
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef BLOCKJOB_INT_H
#define BLOCKJOB_INT_H

#include "block/blockjob.h"

struct BlockJobDriver {
    JobDriver job_driver;

    bool (*drained_poll)(BlockJob *job);

    void (*attached_aio_context)(BlockJob *job, AioContext *new_context);

    void (*set_speed)(BlockJob *job, int64_t speed);

    void (*change)(BlockJob *job, BlockJobChangeOptions *opts, Error **errp);

    void (*query)(BlockJob *job, BlockJobInfo *info);
};

void * GRAPH_UNLOCKED
block_job_create(const char *job_id, const BlockJobDriver *driver,
                 JobTxn *txn, BlockDriverState *bs, uint64_t perm,
                 uint64_t shared_perm, int64_t speed, int flags,
                 BlockCompletionFunc *cb, void *opaque, Error **errp);

void block_job_free(Job *job);

void block_job_user_resume(Job *job);

void block_job_ratelimit_processed_bytes(BlockJob *job, uint64_t n);

void block_job_ratelimit_sleep(BlockJob *job);

BlockErrorAction block_job_error_action(BlockJob *job, BlockdevOnError on_err,
                                        int is_read, int error);

#endif
