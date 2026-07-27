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

#ifndef BLOCKJOB_H
#define BLOCKJOB_H

#include "qapi/qapi-types-block-core.h"
#include "qemu/job.h"
#include "qemu/ratelimit.h"

#define BLOCK_JOB_SLICE_TIME 100000000ULL

typedef struct BlockJobDriver BlockJobDriver;

typedef struct BlockJob {
    Job job;

    BlockDeviceIoStatus iostatus;

    int64_t speed;

    RateLimit limit;

    Error *blocker;

    Notifier finalize_cancelled_notifier;

    Notifier finalize_completed_notifier;

    Notifier pending_notifier;

    Notifier ready_notifier;

    Notifier idle_notifier;

    GSList *nodes;
} BlockJob;

BlockJob *block_job_next_locked(BlockJob *job);

BlockJob *block_job_get(const char *id);

BlockJob *block_job_get_locked(const char *id);

int GRAPH_WRLOCK
block_job_add_bdrv(BlockJob *job, const char *name, BlockDriverState *bs,
                   uint64_t perm, uint64_t shared_perm, Error **errp);

void GRAPH_UNLOCKED block_job_remove_all_bdrv(BlockJob *job);

bool block_job_has_bdrv(BlockJob *job, BlockDriverState *bs);

bool block_job_set_speed_locked(BlockJob *job, int64_t speed, Error **errp);

void block_job_change_locked(BlockJob *job, BlockJobChangeOptions *opts,
                             Error **errp);

BlockJobInfo *block_job_query_locked(BlockJob *job, Error **errp);

void block_job_iostatus_reset_locked(BlockJob *job);

AioContext *block_job_get_aio_context(BlockJob *job);


bool block_job_is_internal(BlockJob *job);

const BlockJobDriver *block_job_driver(BlockJob *job);

#endif
