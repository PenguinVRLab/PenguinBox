/*
 * Migration stats
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_STATS_H
#define QEMU_MIGRATION_STATS_H

#include "qemu/stats64.h"

#define BUFFER_DELAY     100

#define RATE_LIMIT_DISABLED 0

typedef struct {
    Stat64 dirty_bytes_last_sync;
    Stat64 dirty_pages_rate;
    Stat64 dirty_sync_count;
    Stat64 dirty_sync_missed_zero_copy;
    Stat64 downtime_bytes;
    Stat64 multifd_bytes;
    Stat64 normal_pages;
    Stat64 postcopy_bytes;
    Stat64 postcopy_requests;
    Stat64 precopy_bytes;
    Stat64 qemu_file_transferred;
    Stat64 rate_limit_start;
    Stat64 rate_limit_max;
    Stat64 rdma_bytes;
    Stat64 zero_pages;
} MigrationAtomicStats;

extern MigrationAtomicStats mig_stats;

uint64_t migration_rate_get(void);

void migration_rate_reset(void);

void migration_rate_set(uint64_t new_rate);

uint64_t migration_transferred_bytes(void);
#endif
