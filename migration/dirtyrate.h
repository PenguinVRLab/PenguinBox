/*
 *  Dirtyrate common functions
 *
 *  Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.
 *
 *  Authors:
 *  Chuan Zheng <zhengchuan@huawei.com>
 *
 *  This work is licensed under the terms of the GNU GPL, version 2 or later.
 *  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_DIRTYRATE_H
#define QEMU_MIGRATION_DIRTYRATE_H

#include "system/dirtyrate.h"

#define DIRTYRATE_DEFAULT_SAMPLE_PAGES            512

#define RAMBLOCK_INFO_MAX_LEN                     256

#define MIN_RAMBLOCK_SIZE                         128

#define MIN_CALC_TIME_MS                          50
#define MAX_CALC_TIME_MS                       60000

#define MIN_SAMPLE_PAGE_COUNT                     128
#define MAX_SAMPLE_PAGE_COUNT                     16384

struct DirtyRateConfig {
    uint64_t sample_pages_per_gigabytes;
    int64_t calc_time_ms;
    DirtyRateMeasureMode mode;
};

struct RamblockDirtyInfo {
    char idstr[RAMBLOCK_INFO_MAX_LEN];
    uint8_t *ramblock_addr;
    uint64_t ramblock_pages;
    uint64_t *sample_page_vfn;
    uint64_t sample_pages_count;
    uint64_t sample_dirty_count;
    uint32_t *hash_result;
};

typedef struct SampleVMStat {
    uint64_t total_dirty_samples;
    uint64_t total_sample_count;
    uint64_t total_block_mem_MB;
} SampleVMStat;

struct DirtyRateStat {
    int64_t dirty_rate;
    int64_t start_time;
    int64_t calc_time_ms;
    uint64_t sample_pages;
    union {
        SampleVMStat page_sampling;
        VcpuStat dirty_ring;
    };
};

void *get_dirtyrate_thread(void *arg);
#endif
