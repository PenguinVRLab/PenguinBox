/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2011-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
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

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/madvise.h"
#include "qemu/main-loop.h"
#include "xbzrle.h"
#include "ram.h"
#include "migration.h"
#include "migration-stats.h"
#include "migration/register.h"
#include "migration/misc.h"
#include "qemu-file.h"
#include "postcopy-ram.h"
#include "page_cache.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-events-migration.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qmp/qerror.h"
#include "trace.h"
#include "system/ram_addr.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "migration/colo.h"
#include "system/cpu-throttle.h"
#include "system/physmem.h"
#include "system/ramblock.h"
#include "savevm.h"
#include "qemu/iov.h"
#include "multifd.h"
#include "system/runstate.h"
#include "rdma.h"
#include "options.h"
#include "system/dirtylimit.h"
#include "system/kvm.h"

#include "hw/boards.h"

#if defined(__linux__)
#include "qemu/userfaultfd.h"
#endif

#define MAPPED_RAM_FILE_OFFSET_ALIGNMENT 0x100000

#define MAPPED_RAM_LOAD_BUF_SIZE 0x100000

XBZRLECacheStats xbzrle_counters;

struct PageLocation {
    RAMBlock *block;
    unsigned long offset;
};
typedef struct PageLocation PageLocation;

struct PageLocationHint {
    bool valid;
    PageLocation location;
};
typedef struct PageLocationHint PageLocationHint;

struct PageSearchStatus {
    QEMUFile    *pss_channel;
    RAMBlock *last_sent_block;
    RAMBlock    *block;
    unsigned long page;
    bool         complete_round;
    bool          host_page_sending;
    unsigned long host_page_start;
    unsigned long host_page_end;
};
typedef struct PageSearchStatus PageSearchStatus;

static struct {
    uint8_t *encoded_buf;
    uint8_t *current_buf;
    PageCache *cache;
    QemuMutex lock;
    uint8_t *zero_target_page;
    uint8_t *decoded_buf;
} XBZRLE;

static void XBZRLE_cache_lock(void)
{
    if (migrate_xbzrle()) {
        qemu_mutex_lock(&XBZRLE.lock);
    }
}

static void XBZRLE_cache_unlock(void)
{
    if (migrate_xbzrle()) {
        qemu_mutex_unlock(&XBZRLE.lock);
    }
}

int xbzrle_cache_resize(uint64_t new_size, Error **errp)
{
    PageCache *new_cache;
    int64_t ret = 0;

    if (new_size != (size_t)new_size) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                   "exceeding address space");
        return -1;
    }

    if (new_size == migrate_xbzrle_cache_size()) {
        return 0;
    }

    XBZRLE_cache_lock();

    if (XBZRLE.cache != NULL) {
        new_cache = cache_init(new_size, TARGET_PAGE_SIZE, errp);
        if (!new_cache) {
            ret = -1;
            goto out;
        }

        cache_fini(XBZRLE.cache);
        XBZRLE.cache = new_cache;
    }
out:
    XBZRLE_cache_unlock();
    return ret;
}

static bool postcopy_preempt_active(void)
{
    return migrate_postcopy_preempt() && migration_in_postcopy();
}

bool migrate_ram_is_ignored(RAMBlock *block)
{
    MigMode mode = migrate_mode();
    return !qemu_ram_is_migratable(block) ||
           mode == MIG_MODE_CPR_TRANSFER ||
           mode == MIG_MODE_CPR_EXEC ||
           (migrate_ignore_shared() && qemu_ram_is_shared(block)
                                    && qemu_ram_is_named_file(block));
}

#undef RAMBLOCK_FOREACH

int foreach_not_ignored_block(RAMBlockIterFunc func, void *opaque)
{
    RAMBlock *block;
    int ret = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        ret = func(block, opaque);
        if (ret) {
            break;
        }
    }
    return ret;
}

static void ramblock_recv_map_init(void)
{
    RAMBlock *rb;

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        assert(!rb->receivedmap);
        rb->receivedmap = bitmap_new(rb->max_length >> qemu_target_page_bits());
    }
}

int ramblock_recv_bitmap_test(RAMBlock *rb, void *host_addr)
{
    return test_bit(ramblock_recv_bitmap_offset(host_addr, rb),
                    rb->receivedmap);
}

bool ramblock_recv_bitmap_test_byte_offset(RAMBlock *rb, uint64_t byte_offset)
{
    return test_bit(byte_offset >> TARGET_PAGE_BITS, rb->receivedmap);
}

void ramblock_recv_bitmap_set(RAMBlock *rb, void *host_addr)
{
    set_bit_atomic(ramblock_recv_bitmap_offset(host_addr, rb), rb->receivedmap);
}

void ramblock_recv_bitmap_set_range(RAMBlock *rb, void *host_addr,
                                    size_t nr)
{
    bitmap_set_atomic(rb->receivedmap,
                      ramblock_recv_bitmap_offset(host_addr, rb),
                      nr);
}

void ramblock_recv_bitmap_set_offset(RAMBlock *rb, uint64_t byte_offset)
{
    set_bit_atomic(byte_offset >> TARGET_PAGE_BITS, rb->receivedmap);
}
#define  RAMBLOCK_RECV_BITMAP_ENDING  (0x0123456789abcdefULL)

int64_t ramblock_recv_bitmap_send(QEMUFile *file,
                                  const char *block_name)
{
    RAMBlock *block = qemu_ram_block_by_name(block_name);
    unsigned long *le_bitmap, nbits;
    uint64_t size;

    if (!block) {
        error_report("%s: invalid block name: %s", __func__, block_name);
        return -1;
    }

    nbits = block->postcopy_length >> TARGET_PAGE_BITS;

    le_bitmap = bitmap_new(nbits + BITS_PER_LONG);

    bitmap_to_le(le_bitmap, block->receivedmap, nbits);

    size = DIV_ROUND_UP(nbits, 8);

    size = ROUND_UP(size, 8);

    qemu_put_be64(file, size);
    qemu_put_buffer(file, (const uint8_t *)le_bitmap, size);
    g_free(le_bitmap);
    qemu_put_be64(file, RAMBLOCK_RECV_BITMAP_ENDING);
    int ret = qemu_fflush(file);
    if (ret) {
        return ret;
    }

    return size + sizeof(size);
}

struct RAMSrcPageRequest {
    RAMBlock *rb;
    hwaddr    offset;
    hwaddr    len;

    QSIMPLEQ_ENTRY(RAMSrcPageRequest) next_req;
};

struct RAMState {
    PageSearchStatus pss[RAM_CHANNEL_MAX];
    int uffdio_fd;
    uint64_t ram_bytes_total;
    RAMBlock *last_seen_block;
    ram_addr_t last_page;
    uint32_t last_version;
    int dirty_rate_high_cnt;
    int64_t time_last_bitmap_sync;
    uint64_t bytes_xfer_prev;
    uint64_t num_dirty_pages_period;
    uint64_t xbzrle_cache_miss_prev;
    uint64_t xbzrle_pages_prev;
    uint64_t xbzrle_bytes_prev;
    bool xbzrle_started;
    bool last_stage;

    uint64_t target_page_count_prev;
    uint64_t target_page_count;
    uint64_t migration_dirty_pages;
    QemuMutex bitmap_mutex;
    RAMBlock *last_req_rb;
    QemuMutex src_page_req_mutex;
    QSIMPLEQ_HEAD(, RAMSrcPageRequest) src_page_requests;

    unsigned int postcopy_bmap_sync_requested;
    PageLocationHint page_hint;
};
typedef struct RAMState RAMState;

static RAMState *ram_state;

static NotifierWithReturnList precopy_notifier_list;

static bool postcopy_has_request(RAMState *rs)
{
    return !QSIMPLEQ_EMPTY_ATOMIC(&rs->src_page_requests);
}

void precopy_infrastructure_init(void)
{
    notifier_with_return_list_init(&precopy_notifier_list);
}

void precopy_add_notifier(NotifierWithReturn *n)
{
    notifier_with_return_list_add(&precopy_notifier_list, n);
}

void precopy_remove_notifier(NotifierWithReturn *n)
{
    notifier_with_return_remove(n);
}

int precopy_notify(PrecopyNotifyReason reason, Error **errp)
{
    PrecopyNotifyData pnd;
    pnd.reason = reason;

    return notifier_with_return_list_notify(&precopy_notifier_list, &pnd, errp);
}

uint64_t ram_bytes_remaining(void)
{
    return ram_state ? (ram_state->migration_dirty_pages * TARGET_PAGE_SIZE) :
                       0;
}

void ram_transferred_add(uint64_t bytes)
{
    if (runstate_is_running()) {
        stat64_add(&mig_stats.precopy_bytes, bytes);
    } else if (migration_in_postcopy()) {
        stat64_add(&mig_stats.postcopy_bytes, bytes);
    } else {
        stat64_add(&mig_stats.downtime_bytes, bytes);
    }
}

static int ram_save_host_page_urgent(PageSearchStatus *pss);

static void pss_init(PageSearchStatus *pss, RAMBlock *rb, ram_addr_t page)
{
    pss->block = rb;
    pss->page = page;
    pss->complete_round = false;
}

static bool pss_overlap(PageSearchStatus *pss1, PageSearchStatus *pss2)
{
    return pss1->host_page_sending && pss2->host_page_sending &&
        (pss1->host_page_start == pss2->host_page_start);
}

static size_t save_page_header(PageSearchStatus *pss, QEMUFile *f,
                               RAMBlock *block, ram_addr_t offset)
{
    size_t size, len;
    bool same_block = (block == pss->last_sent_block);

    if (same_block) {
        offset |= RAM_SAVE_FLAG_CONTINUE;
    }
    qemu_put_be64(f, offset);
    size = 8;

    if (!same_block) {
        len = strlen(block->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)block->idstr, len);
        size += 1 + len;
        pss->last_sent_block = block;
    }
    return size;
}

static void mig_throttle_guest_down(uint64_t bytes_dirty_period,
                                    uint64_t bytes_dirty_threshold)
{
    uint64_t pct_initial = migrate_cpu_throttle_initial();
    uint64_t pct_increment = migrate_cpu_throttle_increment();
    bool pct_tailslow = migrate_cpu_throttle_tailslow();
    int pct_max = migrate_max_cpu_throttle();

    uint64_t throttle_now = cpu_throttle_get_percentage();
    uint64_t cpu_now, cpu_ideal, throttle_inc;

    if (!cpu_throttle_active()) {
        cpu_throttle_set(pct_initial);
    } else {
        if (!pct_tailslow) {
            throttle_inc = pct_increment;
        } else {
            cpu_now = 100 - throttle_now;
            cpu_ideal = cpu_now * (bytes_dirty_threshold * 1.0 /
                        bytes_dirty_period);
            throttle_inc = MIN(cpu_now - cpu_ideal, pct_increment);
        }
        cpu_throttle_set(MIN(throttle_now + throttle_inc, pct_max));
    }
}

void mig_throttle_counter_reset(void)
{
    RAMState *rs = ram_state;

    rs->time_last_bitmap_sync = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    rs->num_dirty_pages_period = 0;
    rs->bytes_xfer_prev = migration_transferred_bytes();
}

static void xbzrle_cache_zero_page(ram_addr_t current_addr)
{
    cache_insert(XBZRLE.cache, current_addr, XBZRLE.zero_target_page,
                 stat64_get(&mig_stats.dirty_sync_count));
}

#define ENCODING_FLAG_XBZRLE 0x1

static int save_xbzrle_page(RAMState *rs, PageSearchStatus *pss,
                            uint8_t **current_data, ram_addr_t current_addr,
                            RAMBlock *block, ram_addr_t offset)
{
    int encoded_len = 0, bytes_xbzrle;
    uint8_t *prev_cached_page;
    QEMUFile *file = pss->pss_channel;
    uint64_t generation = stat64_get(&mig_stats.dirty_sync_count);

    if (!cache_is_cached(XBZRLE.cache, current_addr, generation)) {
        xbzrle_counters.cache_miss++;
        if (!rs->last_stage) {
            if (cache_insert(XBZRLE.cache, current_addr, *current_data,
                             generation) == -1) {
                return -1;
            } else {
                *current_data = get_cached_data(XBZRLE.cache, current_addr);
            }
        }
        return -1;
    }

    xbzrle_counters.pages++;
    prev_cached_page = get_cached_data(XBZRLE.cache, current_addr);

    memcpy(XBZRLE.current_buf, *current_data, TARGET_PAGE_SIZE);

    encoded_len = xbzrle_encode_buffer(prev_cached_page, XBZRLE.current_buf,
                                       TARGET_PAGE_SIZE, XBZRLE.encoded_buf,
                                       TARGET_PAGE_SIZE);

    if (!rs->last_stage && encoded_len != 0) {
        memcpy(prev_cached_page, XBZRLE.current_buf, TARGET_PAGE_SIZE);
        *current_data = prev_cached_page;
    }

    if (encoded_len == 0) {
        trace_save_xbzrle_page_skipping();
        return 0;
    } else if (encoded_len == -1) {
        trace_save_xbzrle_page_overflow();
        xbzrle_counters.overflow++;
        xbzrle_counters.bytes += TARGET_PAGE_SIZE;
        return -1;
    }

    bytes_xbzrle = save_page_header(pss, pss->pss_channel, block,
                                    offset | RAM_SAVE_FLAG_XBZRLE);
    qemu_put_byte(file, ENCODING_FLAG_XBZRLE);
    qemu_put_be16(file, encoded_len);
    qemu_put_buffer(file, XBZRLE.encoded_buf, encoded_len);
    bytes_xbzrle += encoded_len + 1 + 2;
    xbzrle_counters.bytes += bytes_xbzrle - 8;
    ram_transferred_add(bytes_xbzrle);

    return 1;
}

static void pss_find_next_dirty(PageSearchStatus *pss)
{
    RAMBlock *rb = pss->block;
    unsigned long size = rb->used_length >> TARGET_PAGE_BITS;
    unsigned long *bitmap = rb->bmap;

    if (migrate_ram_is_ignored(rb)) {
        pss->page = size;
        return;
    }

    if (pss->host_page_sending) {
        assert(pss->host_page_end);
        size = MIN(size, pss->host_page_end);
    }

    pss->page = find_next_bit(bitmap, size, pss->page);
}

static void migration_clear_memory_region_dirty_bitmap(RAMBlock *rb,
                                                       unsigned long page)
{
    uint8_t shift;
    hwaddr size, start;

    if (!rb->clear_bmap || !clear_bmap_test_and_clear(rb, page)) {
        return;
    }

    shift = rb->clear_bmap_shift;
    assert(shift >= 6);

    size = 1ULL << (TARGET_PAGE_BITS + shift);
    start = QEMU_ALIGN_DOWN((ram_addr_t)page << TARGET_PAGE_BITS, size);
    trace_migration_bitmap_clear_dirty(rb->idstr, start, size, page);
    memory_region_clear_dirty_bitmap(rb->mr, start, size);
}

static void
migration_clear_memory_region_dirty_bitmap_range(RAMBlock *rb,
                                                 unsigned long start,
                                                 unsigned long npages)
{
    unsigned long i, chunk_pages = 1UL << rb->clear_bmap_shift;
    unsigned long chunk_start = QEMU_ALIGN_DOWN(start, chunk_pages);
    unsigned long chunk_end = QEMU_ALIGN_UP(start + npages, chunk_pages);

    for (i = chunk_start; i < chunk_end; i += chunk_pages) {
        migration_clear_memory_region_dirty_bitmap(rb, i);
    }
}

static inline
unsigned long colo_bitmap_find_dirty(RAMState *rs, RAMBlock *rb,
                                     unsigned long start, unsigned long *num)
{
    unsigned long size = rb->used_length >> TARGET_PAGE_BITS;
    unsigned long *bitmap = rb->bmap;
    unsigned long first, next;

    *num = 0;

    if (migrate_ram_is_ignored(rb)) {
        return size;
    }

    first = find_next_bit(bitmap, size, start);
    if (first >= size) {
        return first;
    }
    next = find_next_zero_bit(bitmap, size, first + 1);
    assert(next >= first);
    *num = next - first;
    return first;
}

static inline bool migration_bitmap_clear_dirty(RAMState *rs,
                                                RAMBlock *rb,
                                                unsigned long page)
{
    bool ret;

    if (!rs->last_stage && !migration_in_postcopy()) {
        migration_clear_memory_region_dirty_bitmap(rb, page);
    }

    ret = test_and_clear_bit(page, rb->bmap);
    if (ret) {
        rs->migration_dirty_pages--;
    }

    return ret;
}

static int dirty_bitmap_clear_section(MemoryRegionSection *section,
                                      void *opaque)
{
    const hwaddr offset = section->offset_within_region;
    const hwaddr size = int128_get64(section->size);
    const unsigned long start = offset >> TARGET_PAGE_BITS;
    const unsigned long npages = size >> TARGET_PAGE_BITS;
    RAMBlock *rb = section->mr->ram_block;
    uint64_t *cleared_bits = opaque;

    if (!migration_in_postcopy() && !migrate_background_snapshot()) {
        migration_clear_memory_region_dirty_bitmap_range(rb, start, npages);
    }
    *cleared_bits += bitmap_count_one_with_offset(rb->bmap, start, npages);
    bitmap_clear(rb->bmap, start, npages);
    return 0;
}

static uint64_t ramblock_dirty_bitmap_clear_discarded_pages(RAMBlock *rb)
{
    uint64_t cleared_bits = 0;

    if (rb->mr && rb->bmap && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = 0,
            .size = int128_make64(qemu_ram_get_used_length(rb)),
        };

        ram_discard_manager_replay_discarded(rdm, &section,
                                             dirty_bitmap_clear_section,
                                             &cleared_bits);
    }
    return cleared_bits;
}

bool ramblock_page_is_discarded(RAMBlock *rb, ram_addr_t start)
{
    if (rb->mr && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = start,
            .size = int128_make64(qemu_ram_pagesize(rb)),
        };

        return !ram_discard_manager_is_populated(rdm, &section);
    }
    return false;
}

static uint64_t physical_memory_sync_dirty_bitmap(RAMBlock *rb,
                                                  ram_addr_t start,
                                                  ram_addr_t length)
{
    ram_addr_t addr;
    unsigned long word = BIT_WORD((start + rb->offset) >> TARGET_PAGE_BITS);
    uint64_t num_dirty = 0;
    unsigned long *dest = rb->bmap;

    if (((word * BITS_PER_LONG) << TARGET_PAGE_BITS) ==
         (start + rb->offset) &&
        !(length & ((BITS_PER_LONG << TARGET_PAGE_BITS) - 1))) {
        int k;
        int nr = BITS_TO_LONGS(length >> TARGET_PAGE_BITS);
        unsigned long * const *src;
        unsigned long idx = (word * BITS_PER_LONG) / DIRTY_MEMORY_BLOCK_SIZE;
        unsigned long offset = BIT_WORD((word * BITS_PER_LONG) %
                                        DIRTY_MEMORY_BLOCK_SIZE);
        unsigned long page = BIT_WORD(start >> TARGET_PAGE_BITS);

        src = qatomic_rcu_read(
                &ram_list.dirty_memory[DIRTY_MEMORY_MIGRATION])->blocks;

        for (k = page; k < page + nr; k++) {
            if (src[idx][offset]) {
                unsigned long bits = qatomic_xchg(&src[idx][offset], 0);
                unsigned long new_dirty;
                new_dirty = ~dest[k];
                dest[k] |= bits;
                new_dirty &= bits;
                num_dirty += ctpopl(new_dirty);
            }

            if (++offset >= BITS_TO_LONGS(DIRTY_MEMORY_BLOCK_SIZE)) {
                offset = 0;
                idx++;
            }
        }
        if (num_dirty) {
            physical_memory_dirty_bits_cleared(start, length);
        }

        if (rb->clear_bmap) {
            clear_bmap_set(rb, start >> TARGET_PAGE_BITS,
                           length >> TARGET_PAGE_BITS);
        } else {
            memory_region_clear_dirty_bitmap(rb->mr, start, length);
        }
    } else {
        ram_addr_t offset = rb->offset;

        for (addr = 0; addr < length; addr += TARGET_PAGE_SIZE) {
            if (physical_memory_test_and_clear_dirty(
                        start + addr + offset,
                        TARGET_PAGE_SIZE,
                        DIRTY_MEMORY_MIGRATION)) {
                long k = (start + addr) >> TARGET_PAGE_BITS;
                if (!test_and_set_bit(k, dest)) {
                    num_dirty++;
                }
            }
        }
    }

    return num_dirty;
}

static void ramblock_sync_dirty_bitmap(RAMState *rs, RAMBlock *rb)
{
    uint64_t new_dirty_pages =
        physical_memory_sync_dirty_bitmap(rb, 0, rb->used_length);

    rs->migration_dirty_pages += new_dirty_pages;
    rs->num_dirty_pages_period += new_dirty_pages;
}

uint64_t ram_pagesize_summary(void)
{
    RAMBlock *block;
    uint64_t summary = 0;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        summary |= block->page_size;
    }

    return summary;
}

uint64_t ram_get_total_transferred_pages(void)
{
    return stat64_get(&mig_stats.normal_pages) +
        stat64_get(&mig_stats.zero_pages) +
        xbzrle_counters.pages;
}

static void migration_update_rates(RAMState *rs, int64_t end_time)
{
    uint64_t page_count = rs->target_page_count - rs->target_page_count_prev;

    stat64_set(&mig_stats.dirty_pages_rate,
               rs->num_dirty_pages_period * 1000 /
               (end_time - rs->time_last_bitmap_sync));

    if (!page_count) {
        return;
    }

    if (migrate_xbzrle()) {
        double encoded_size, unencoded_size;

        xbzrle_counters.cache_miss_rate = (double)(xbzrle_counters.cache_miss -
            rs->xbzrle_cache_miss_prev) / page_count;
        rs->xbzrle_cache_miss_prev = xbzrle_counters.cache_miss;
        unencoded_size = (xbzrle_counters.pages - rs->xbzrle_pages_prev) *
                         TARGET_PAGE_SIZE;
        encoded_size = xbzrle_counters.bytes - rs->xbzrle_bytes_prev;
        if (xbzrle_counters.pages == rs->xbzrle_pages_prev || !encoded_size) {
            xbzrle_counters.encoding_rate = 0;
        } else {
            xbzrle_counters.encoding_rate = unencoded_size / encoded_size;
        }
        rs->xbzrle_pages_prev = xbzrle_counters.pages;
        rs->xbzrle_bytes_prev = xbzrle_counters.bytes;
    }
}

static void migration_dirty_limit_guest(void)
{
    static int64_t quota_dirtyrate;
    MigrationState *s = migrate_get_current();

    if (dirtylimit_in_service() &&
        quota_dirtyrate == s->parameters.vcpu_dirty_limit) {
        return;
    }

    quota_dirtyrate = s->parameters.vcpu_dirty_limit;

    qmp_set_vcpu_dirty_limit(false, -1, quota_dirtyrate, NULL);
    trace_migration_dirty_limit_guest(quota_dirtyrate);
}

static void migration_trigger_throttle(RAMState *rs)
{
    uint64_t threshold = migrate_throttle_trigger_threshold();
    uint64_t bytes_xfer_period =
        migration_transferred_bytes() - rs->bytes_xfer_prev;
    uint64_t bytes_dirty_period = rs->num_dirty_pages_period * TARGET_PAGE_SIZE;
    uint64_t bytes_dirty_threshold = bytes_xfer_period * threshold / 100;

    if ((bytes_dirty_period > bytes_dirty_threshold) &&
        (++rs->dirty_rate_high_cnt >= 2)) {
        rs->dirty_rate_high_cnt = 0;
        if (migrate_auto_converge()) {
            trace_migration_throttle();
            mig_throttle_guest_down(bytes_dirty_period,
                                    bytes_dirty_threshold);
        } else if (migrate_dirty_limit()) {
            migration_dirty_limit_guest();
        }
    }
}

static void migration_bitmap_sync(RAMState *rs, bool last_stage)
{
    RAMBlock *block;
    int64_t end_time;

    stat64_add(&mig_stats.dirty_sync_count, 1);

    if (!rs->time_last_bitmap_sync) {
        rs->time_last_bitmap_sync = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    }

    trace_migration_bitmap_sync_start();
    memory_global_dirty_log_sync(last_stage);

    WITH_QEMU_LOCK_GUARD(&rs->bitmap_mutex) {
        WITH_RCU_READ_LOCK_GUARD() {
            RAMBLOCK_FOREACH_NOT_IGNORED(block) {
                ramblock_sync_dirty_bitmap(rs, block);
            }
            stat64_set(&mig_stats.dirty_bytes_last_sync, ram_bytes_remaining());
        }
    }

    memory_global_after_dirty_log_sync();
    trace_migration_bitmap_sync_end(rs->num_dirty_pages_period);

    end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    if (end_time > rs->time_last_bitmap_sync + 1000) {
        migration_trigger_throttle(rs);

        migration_update_rates(rs, end_time);

        rs->target_page_count_prev = rs->target_page_count;

        rs->time_last_bitmap_sync = end_time;
        rs->num_dirty_pages_period = 0;
        rs->bytes_xfer_prev = migration_transferred_bytes();
    }
    if (migrate_events()) {
        uint64_t generation = stat64_get(&mig_stats.dirty_sync_count);
        qapi_event_send_migration_pass(generation);
    }
}

void migration_bitmap_sync_precopy(bool last_stage)
{
    Error *local_err = NULL;
    assert(ram_state);

    if (precopy_notify(PRECOPY_NOTIFY_BEFORE_BITMAP_SYNC, &local_err)) {
        error_report_err(local_err);
        local_err = NULL;
    }

    migration_bitmap_sync(ram_state, last_stage);

    if (precopy_notify(PRECOPY_NOTIFY_AFTER_BITMAP_SYNC, &local_err)) {
        error_report_err(local_err);
    }
}

void ram_release_page(const char *rbname, uint64_t offset)
{
    if (!migrate_release_ram() || !migration_in_postcopy()) {
        return;
    }

    ram_discard_range(rbname, offset, TARGET_PAGE_SIZE);
}

static int save_zero_page(RAMState *rs, PageSearchStatus *pss,
                          ram_addr_t offset)
{
    uint8_t *p = pss->block->host + offset;
    QEMUFile *file = pss->pss_channel;
    int len = 0;

    if (migrate_zero_page_detection() == ZERO_PAGE_DETECTION_NONE) {
        return 0;
    }

    if (!buffer_is_zero(p, TARGET_PAGE_SIZE)) {
        return 0;
    }

    stat64_add(&mig_stats.zero_pages, 1);

    if (migrate_mapped_ram()) {
        clear_bit_atomic(offset >> TARGET_PAGE_BITS, pss->block->file_bmap);
        return 1;
    }

    len += save_page_header(pss, file, pss->block, offset | RAM_SAVE_FLAG_ZERO);
    qemu_put_byte(file, 0);
    len += 1;
    ram_release_page(pss->block->idstr, offset);
    ram_transferred_add(len);

    if (rs->xbzrle_started) {
        XBZRLE_cache_lock();
        xbzrle_cache_zero_page(pss->block->offset + offset);
        XBZRLE_cache_unlock();
    }

    return len;
}

static int save_normal_page(PageSearchStatus *pss, RAMBlock *block,
                            ram_addr_t offset, uint8_t *buf, bool async)
{
    QEMUFile *file = pss->pss_channel;

    if (migrate_mapped_ram()) {
        qemu_put_buffer_at(file, buf, TARGET_PAGE_SIZE,
                           block->pages_offset + offset);
        set_bit(offset >> TARGET_PAGE_BITS, block->file_bmap);
    } else {
        ram_transferred_add(save_page_header(pss, pss->pss_channel, block,
                                             offset | RAM_SAVE_FLAG_PAGE));
        if (async) {
            qemu_put_buffer_async(file, buf, TARGET_PAGE_SIZE,
                                  migrate_release_ram() &&
                                  migration_in_postcopy());
        } else {
            qemu_put_buffer(file, buf, TARGET_PAGE_SIZE);
        }
    }
    ram_transferred_add(TARGET_PAGE_SIZE);
    stat64_add(&mig_stats.normal_pages, 1);
    return 1;
}

static int ram_save_page(RAMState *rs, PageSearchStatus *pss)
{
    int pages = -1;
    uint8_t *p;
    bool send_async = true;
    RAMBlock *block = pss->block;
    ram_addr_t offset = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;
    ram_addr_t current_addr = block->offset + offset;

    p = block->host + offset;
    trace_ram_save_page(block->idstr, (uint64_t)offset, p);

    XBZRLE_cache_lock();
    if (rs->xbzrle_started && !migration_in_postcopy()) {
        pages = save_xbzrle_page(rs, pss, &p, current_addr,
                                 block, offset);
        if (!rs->last_stage) {
            send_async = false;
        }
    }

    if (pages == -1) {
        pages = save_normal_page(pss, block, offset, p, send_async);
    }

    XBZRLE_cache_unlock();

    return pages;
}

static int ram_save_multifd_page(RAMBlock *block, ram_addr_t offset)
{
    if (!multifd_queue_page(block, offset)) {
        return -1;
    }

    return 1;
}


#define PAGE_ALL_CLEAN 0
#define PAGE_TRY_AGAIN 1
#define PAGE_DIRTY_FOUND 2
static int find_dirty_block(RAMState *rs, PageSearchStatus *pss)
{
    pss_find_next_dirty(pss);

    if (pss->complete_round && pss->block == rs->last_seen_block &&
        pss->page >= rs->last_page) {
        return PAGE_ALL_CLEAN;
    }
    if (!offset_in_ramblock(pss->block,
                            ((ram_addr_t)pss->page) << TARGET_PAGE_BITS)) {
        pss->page = 0;
        pss->block = QLIST_NEXT_RCU(pss->block, next);
        if (!pss->block) {
            if (multifd_ram_sync_per_round()) {
                QEMUFile *f = rs->pss[RAM_CHANNEL_PRECOPY].pss_channel;
                int ret = multifd_ram_flush_and_sync(f);
                if (ret < 0) {
                    return ret;
                }
            }

            pss->block = QLIST_FIRST_RCU(&ram_list.blocks);
            pss->complete_round = true;
            if (migrate_xbzrle()) {
                rs->xbzrle_started = true;
            }
        }
        return PAGE_TRY_AGAIN;
    } else {
        return PAGE_DIRTY_FOUND;
    }
}

static RAMBlock *unqueue_page(RAMState *rs, ram_addr_t *offset)
{
    struct RAMSrcPageRequest *entry;
    RAMBlock *block = NULL;

    if (!postcopy_has_request(rs)) {
        return NULL;
    }

    QEMU_LOCK_GUARD(&rs->src_page_req_mutex);

    assert(postcopy_has_request(rs));

    entry = QSIMPLEQ_FIRST(&rs->src_page_requests);
    block = entry->rb;
    *offset = entry->offset;

    if (entry->len > TARGET_PAGE_SIZE) {
        entry->len -= TARGET_PAGE_SIZE;
        entry->offset += TARGET_PAGE_SIZE;
    } else {
        memory_region_unref(block->mr);
        QSIMPLEQ_REMOVE_HEAD(&rs->src_page_requests, next_req);
        g_free(entry);
        migration_consume_urgent_request();
    }

    return block;
}

#if defined(__linux__)
static RAMBlock *poll_fault_page(RAMState *rs, ram_addr_t *offset)
{
    struct uffd_msg uffd_msg;
    void *page_address;
    RAMBlock *block;
    int res;

    if (!migrate_background_snapshot()) {
        return NULL;
    }

    res = uffd_read_events(rs->uffdio_fd, &uffd_msg, 1);
    if (res <= 0) {
        return NULL;
    }

    page_address = (void *)(uintptr_t) uffd_msg.arg.pagefault.address;
    block = qemu_ram_block_from_host(page_address, false, offset);
    assert(block && (block->flags & RAM_UF_WRITEPROTECT) != 0);
    return block;
}

static int ram_save_release_protection(RAMState *rs, PageSearchStatus *pss,
        unsigned long start_page)
{
    int res = 0;

    if (pss->block->flags & RAM_UF_WRITEPROTECT) {
        void *page_address = pss->block->host + (start_page << TARGET_PAGE_BITS);
        uint64_t run_length = (pss->page - start_page) << TARGET_PAGE_BITS;

        qemu_fflush(pss->pss_channel);
        res = uffd_change_protection(rs->uffdio_fd, page_address, run_length,
                false, false);
    }

    return res;
}

bool ram_write_tracking_available(void)
{
    uint64_t uffd_features;
    int res;

    res = uffd_query_features(&uffd_features);
    return (res == 0 &&
            (uffd_features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) != 0);
}

bool ram_write_tracking_compatible(void)
{
    const uint64_t uffd_ioctls_mask = BIT(_UFFDIO_WRITEPROTECT);
    int uffd_fd;
    RAMBlock *block;
    bool ret = false;

    uffd_fd = uffd_create_fd(UFFD_FEATURE_PAGEFAULT_FLAG_WP, false);
    if (uffd_fd < 0) {
        return false;
    }

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        uint64_t uffd_ioctls;

        if (block->mr->readonly || block->mr->rom_device) {
            continue;
        }
        if (uffd_register_memory(uffd_fd, block->host, block->max_length,
                UFFDIO_REGISTER_MODE_WP, &uffd_ioctls)) {
            goto out;
        }
        if ((uffd_ioctls & uffd_ioctls_mask) != uffd_ioctls_mask) {
            goto out;
        }
    }
    ret = true;

out:
    uffd_close_fd(uffd_fd);
    return ret;
}

static inline void populate_read_range(RAMBlock *block, ram_addr_t offset,
                                       ram_addr_t size)
{
    const ram_addr_t end = offset + size;

    for (; offset < end; offset += block->page_size) {
        char tmp = *((char *)block->host + offset);

        asm volatile("" : "+r" (tmp));
    }
}

static inline int populate_read_section(MemoryRegionSection *section,
                                        void *opaque)
{
    const hwaddr size = int128_get64(section->size);
    hwaddr offset = section->offset_within_region;
    RAMBlock *block = section->mr->ram_block;

    populate_read_range(block, offset, size);
    return 0;
}

static void ram_block_populate_read(RAMBlock *rb)
{
    if (rb->mr && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = 0,
            .size = rb->mr->size,
        };

        ram_discard_manager_replay_populated(rdm, &section,
                                             populate_read_section, NULL);
    } else {
        populate_read_range(rb, 0, rb->used_length);
    }
}

void ram_write_tracking_prepare(void)
{
    RAMBlock *block;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if (block->mr->readonly || block->mr->rom_device) {
            continue;
        }

        ram_block_populate_read(block);
    }
}

static inline int uffd_protect_section(MemoryRegionSection *section,
                                       void *opaque)
{
    const hwaddr size = int128_get64(section->size);
    const hwaddr offset = section->offset_within_region;
    RAMBlock *rb = section->mr->ram_block;
    int uffd_fd = (uintptr_t)opaque;

    return uffd_change_protection(uffd_fd, rb->host + offset, size, true,
                                  false);
}

static int ram_block_uffd_protect(RAMBlock *rb, int uffd_fd)
{
    assert(rb->flags & RAM_UF_WRITEPROTECT);

    if (rb->mr && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = 0,
            .size = rb->mr->size,
        };

        return ram_discard_manager_replay_populated(rdm, &section,
                                                    uffd_protect_section,
                                                    (void *)(uintptr_t)uffd_fd);
    }
    return uffd_change_protection(uffd_fd, rb->host,
                                  rb->used_length, true, false);
}

int ram_write_tracking_start(void)
{
    int uffd_fd;
    RAMState *rs = ram_state;
    RAMBlock *block;

    uffd_fd = uffd_create_fd(UFFD_FEATURE_PAGEFAULT_FLAG_WP, true);
    if (uffd_fd < 0) {
        return uffd_fd;
    }
    rs->uffdio_fd = uffd_fd;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if (block->mr->readonly || block->mr->rom_device) {
            continue;
        }

        if (uffd_register_memory(rs->uffdio_fd, block->host,
                block->max_length, UFFDIO_REGISTER_MODE_WP, NULL)) {
            goto fail;
        }
        block->flags |= RAM_UF_WRITEPROTECT;
        memory_region_ref(block->mr);

        if (ram_block_uffd_protect(block, uffd_fd)) {
            goto fail;
        }

        trace_ram_write_tracking_ramblock_start(block->idstr, block->page_size,
                block->host, block->max_length);
    }

    return 0;

fail:
    error_report("ram_write_tracking_start() failed: restoring initial memory state");

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if ((block->flags & RAM_UF_WRITEPROTECT) == 0) {
            continue;
        }
        uffd_unregister_memory(rs->uffdio_fd, block->host, block->max_length);
        block->flags &= ~RAM_UF_WRITEPROTECT;
        memory_region_unref(block->mr);
    }

    uffd_close_fd(uffd_fd);
    rs->uffdio_fd = -1;
    return -1;
}

void ram_write_tracking_stop(void)
{
    RAMState *rs = ram_state;
    RAMBlock *block;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if ((block->flags & RAM_UF_WRITEPROTECT) == 0) {
            continue;
        }
        uffd_unregister_memory(rs->uffdio_fd, block->host, block->max_length);

        trace_ram_write_tracking_ramblock_stop(block->idstr, block->page_size,
                block->host, block->max_length);

        block->flags &= ~RAM_UF_WRITEPROTECT;
        memory_region_unref(block->mr);
    }

    uffd_close_fd(rs->uffdio_fd);
    rs->uffdio_fd = -1;
}

#else

static RAMBlock *poll_fault_page(RAMState *rs, ram_addr_t *offset)
{
    (void) rs;
    (void) offset;

    return NULL;
}

static int ram_save_release_protection(RAMState *rs, PageSearchStatus *pss,
        unsigned long start_page)
{
    (void) rs;
    (void) pss;
    (void) start_page;

    return 0;
}

bool ram_write_tracking_available(void)
{
    return false;
}

bool ram_write_tracking_compatible(void)
{
    g_assert_not_reached();
}

int ram_write_tracking_start(void)
{
    g_assert_not_reached();
}

void ram_write_tracking_stop(void)
{
    g_assert_not_reached();
}
#endif

static bool get_queued_page(RAMState *rs, PageSearchStatus *pss)
{
    RAMBlock  *block;
    ram_addr_t offset;
    bool dirty = false;

    do {
        block = unqueue_page(rs, &offset);
        if (block) {
            unsigned long page;

            page = offset >> TARGET_PAGE_BITS;
            dirty = test_bit(page, block->bmap);
            if (!dirty) {
                trace_get_queued_page_not_dirty(block->idstr, (uint64_t)offset,
                                                page);
            } else {
                trace_get_queued_page(block->idstr, (uint64_t)offset, page);
            }
        }

    } while (block && !dirty);

    if (!block) {
        block = poll_fault_page(rs, &offset);
    }

    if (block) {
        pss->block = block;
        pss->page = offset >> TARGET_PAGE_BITS;

        pss->complete_round = false;
    }

    return !!block;
}

static void migration_page_queue_free(RAMState *rs)
{
    struct RAMSrcPageRequest *mspr, *next_mspr;
    RCU_READ_LOCK_GUARD();
    QSIMPLEQ_FOREACH_SAFE(mspr, &rs->src_page_requests, next_req, next_mspr) {
        memory_region_unref(mspr->rb->mr);
        QSIMPLEQ_REMOVE_HEAD(&rs->src_page_requests, next_req);
        g_free(mspr);
    }
}

int ram_save_queue_pages(const char *rbname, ram_addr_t start, ram_addr_t len,
                         Error **errp)
{
    RAMBlock *ramblock;
    RAMState *rs = ram_state;

    stat64_add(&mig_stats.postcopy_requests, 1);
    RCU_READ_LOCK_GUARD();

    if (!rbname) {
        ramblock = rs->last_req_rb;

        if (!ramblock) {
            error_setg(errp, "MIG_RP_MSG_REQ_PAGES has no previous block");
            return -1;
        }
    } else {
        ramblock = qemu_ram_block_by_name(rbname);

        if (!ramblock) {
            error_setg(errp, "MIG_RP_MSG_REQ_PAGES has no block '%s'", rbname);
            return -1;
        }
        rs->last_req_rb = ramblock;
    }
    trace_ram_save_queue_pages(ramblock->idstr, start, len);
    if (!offset_in_ramblock(ramblock, start + len - 1)) {
        error_setg(errp, "MIG_RP_MSG_REQ_PAGES request overrun, "
                   "start=" RAM_ADDR_FMT " len="
                   RAM_ADDR_FMT " blocklen=" RAM_ADDR_FMT,
                   start, len, ramblock->used_length);
        return -1;
    }

    if (postcopy_preempt_active()) {
        ram_addr_t page_start = start >> TARGET_PAGE_BITS;
        size_t page_size = qemu_ram_pagesize(ramblock);
        PageSearchStatus *pss = &ram_state->pss[RAM_CHANNEL_POSTCOPY];
        int ret = 0;

        qemu_mutex_lock(&rs->bitmap_mutex);

        pss_init(pss, ramblock, page_start);
        pss->pss_channel = migrate_get_current()->postcopy_qemufile_src;
        assert(pss->pss_channel);

        assert(len % page_size == 0);
        while (len) {
            if (ram_save_host_page_urgent(pss)) {
                error_setg(errp, "ram_save_host_page_urgent() failed: "
                           "ramblock=%s, start_addr=0x"RAM_ADDR_FMT,
                           ramblock->idstr, start);
                ret = -1;
                break;
            }
            len -= page_size;
        };
        qemu_mutex_unlock(&rs->bitmap_mutex);

        return ret;
    }

    struct RAMSrcPageRequest *new_entry =
        g_new0(struct RAMSrcPageRequest, 1);
    new_entry->rb = ramblock;
    new_entry->offset = start;
    new_entry->len = len;

    memory_region_ref(ramblock->mr);
    qemu_mutex_lock(&rs->src_page_req_mutex);
    QSIMPLEQ_INSERT_TAIL(&rs->src_page_requests, new_entry, next_req);
    migration_make_urgent_request();
    qemu_mutex_unlock(&rs->src_page_req_mutex);

    return 0;
}

static int ram_save_target_page(RAMState *rs, PageSearchStatus *pss)
{
    ram_addr_t offset = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;
    int res;

    if (migrate_rdma()) {
        res = rdma_control_save_page(pss->pss_channel, pss->block->offset,
                                     offset, TARGET_PAGE_SIZE);

        if (res == RAM_SAVE_CONTROL_DELAYED) {
            res = 1;
        }
        return res;
    }

    if (!migrate_multifd()
        || migrate_zero_page_detection() == ZERO_PAGE_DETECTION_LEGACY) {
        if (save_zero_page(rs, pss, offset)) {
            return 1;
        }
    }

    if (migrate_multifd() && !migration_in_postcopy()) {
        return ram_save_multifd_page(pss->block, offset);
    }

    return ram_save_page(rs, pss);
}

static void pss_host_page_prepare(PageSearchStatus *pss)
{
    size_t guest_pfns = qemu_ram_pagesize(pss->block) >> TARGET_PAGE_BITS;

    pss->host_page_sending = true;
    if (guest_pfns <= 1) {
        pss->host_page_start = pss->page;
        pss->host_page_end = pss->page + 1;
    } else {
        pss->host_page_start = ROUND_DOWN(pss->page, guest_pfns);
        pss->host_page_end = ROUND_UP(pss->page + 1, guest_pfns);
    }
}

static bool pss_within_range(PageSearchStatus *pss)
{
    ram_addr_t ram_addr;

    assert(pss->host_page_sending);

    if (pss->page >= pss->host_page_end) {
        return false;
    }

    ram_addr = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;

    return offset_in_ramblock(pss->block, ram_addr);
}

static void pss_host_page_finish(PageSearchStatus *pss)
{
    pss->host_page_sending = false;
    pss->host_page_start = pss->host_page_end = 0;
}

static void ram_page_hint_update(RAMState *rs, PageSearchStatus *pss)
{
    PageLocationHint *hint = &rs->page_hint;

    if (hint->valid) {
        return;
    }

    hint->location.block = pss->block;
    hint->location.offset = pss->page;
    hint->valid = true;
}

static int ram_save_host_page_urgent(PageSearchStatus *pss)
{
    bool page_dirty, sent = false;
    RAMState *rs = ram_state;
    int ret = 0;

    trace_postcopy_preempt_send_host_page(pss->block->idstr, pss->page);
    pss_host_page_prepare(pss);

    if (pss_overlap(pss, &ram_state->pss[RAM_CHANNEL_PRECOPY])) {
        trace_postcopy_preempt_hit(pss->block->idstr,
                                   pss->page << TARGET_PAGE_BITS);
        return 0;
    }

    do {
        page_dirty = migration_bitmap_clear_dirty(rs, pss->block, pss->page);

        if (page_dirty) {
            if (ram_save_target_page(rs, pss) != 1) {
                error_report_once("%s: ram_save_target_page failed", __func__);
                ret = -1;
                goto out;
            }
            sent = true;
        }
        pss_find_next_dirty(pss);
    } while (pss_within_range(pss));
out:
    pss_host_page_finish(pss);
    if (sent) {
        qemu_fflush(pss->pss_channel);
        ram_page_hint_update(rs, pss);
    }
    return ret;
}

static int ram_save_host_page(RAMState *rs, PageSearchStatus *pss)
{
    bool page_dirty, preempt_active = postcopy_preempt_active();
    int tmppages, pages = 0;
    size_t pagesize_bits =
        qemu_ram_pagesize(pss->block) >> TARGET_PAGE_BITS;
    unsigned long start_page = pss->page;
    int res;

    if (migrate_ram_is_ignored(pss->block)) {
        error_report("block %s should not be migrated !", pss->block->idstr);
        return 0;
    }

    pss_host_page_prepare(pss);

    do {
        page_dirty = migration_bitmap_clear_dirty(rs, pss->block, pss->page);

        if (page_dirty) {
            if (preempt_active) {
                qemu_mutex_unlock(&rs->bitmap_mutex);
            }
            tmppages = ram_save_target_page(rs, pss);
            if (tmppages >= 0) {
                pages += tmppages;
                if (pagesize_bits > 1 && tmppages > 0) {
                    migration_rate_limit();
                }
            }
            if (preempt_active) {
                qemu_mutex_lock(&rs->bitmap_mutex);
            }
        } else {
            tmppages = 0;
        }

        if (tmppages < 0) {
            pss_host_page_finish(pss);
            return tmppages;
        }

        pss_find_next_dirty(pss);
    } while (pss_within_range(pss));

    pss_host_page_finish(pss);

    res = ram_save_release_protection(rs, pss, start_page);
    return (res < 0 ? res : pages);
}

static bool ram_page_hint_valid(RAMState *rs)
{
    if (!postcopy_preempt_active()) {
        return false;
    }

    return rs->page_hint.valid;
}

static void ram_page_hint_collect(RAMState *rs, RAMBlock **block,
                                  unsigned long *page)
{
    PageLocationHint *hint = &rs->page_hint;

    assert(hint->valid);

    *block = hint->location.block;
    *page = hint->location.offset;

    hint->valid = false;
}

static int ram_find_and_save_block(RAMState *rs)
{
    PageSearchStatus *pss = &rs->pss[RAM_CHANNEL_PRECOPY];
    unsigned long next_page;
    RAMBlock *next_block;
    int pages = 0;

    if (!rs->ram_bytes_total) {
        return pages;
    }

    if (!rs->last_seen_block) {
        rs->last_seen_block = QLIST_FIRST_RCU(&ram_list.blocks);
        rs->last_page = 0;
    }

    if (ram_page_hint_valid(rs)) {
        ram_page_hint_collect(rs, &next_block, &next_page);
    } else {
        next_block = rs->last_seen_block;
        next_page = rs->last_page;
    }

    pss_init(pss, next_block, next_page);

    while (true){
        if (!get_queued_page(rs, pss)) {
            int res = find_dirty_block(rs, pss);

            if (res == PAGE_ALL_CLEAN) {
                break;
            } else if (res == PAGE_TRY_AGAIN) {
                continue;
            } else if (res < 0) {
                pages = res;
                break;
            }

            assert(res == PAGE_DIRTY_FOUND);
        }
        pages = ram_save_host_page(rs, pss);
        if (pages) {
            break;
        }
    }

    rs->last_seen_block = pss->block;
    rs->last_page = pss->page;

    return pages;
}

static uint64_t ram_bytes_total_with_ignored(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        total += block->used_length;
    }
    return total;
}

uint64_t ram_bytes_total(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        total += block->used_length;
    }
    return total;
}

static void xbzrle_load_setup(void)
{
    XBZRLE.decoded_buf = g_malloc(TARGET_PAGE_SIZE);
}

static void xbzrle_load_cleanup(void)
{
    g_free(XBZRLE.decoded_buf);
    XBZRLE.decoded_buf = NULL;
}

static void ram_state_cleanup(RAMState **rsp)
{
    if (*rsp) {
        migration_page_queue_free(*rsp);
        qemu_mutex_destroy(&(*rsp)->bitmap_mutex);
        qemu_mutex_destroy(&(*rsp)->src_page_req_mutex);
        g_free(*rsp);
        *rsp = NULL;
    }
}

static void xbzrle_cleanup(void)
{
    XBZRLE_cache_lock();
    if (XBZRLE.cache) {
        cache_fini(XBZRLE.cache);
        g_free(XBZRLE.encoded_buf);
        g_free(XBZRLE.current_buf);
        g_free(XBZRLE.zero_target_page);
        XBZRLE.cache = NULL;
        XBZRLE.encoded_buf = NULL;
        XBZRLE.current_buf = NULL;
        XBZRLE.zero_target_page = NULL;
    }
    XBZRLE_cache_unlock();
}

static void ram_bitmaps_destroy(void)
{
    RAMBlock *block;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        g_free(block->clear_bmap);
        block->clear_bmap = NULL;
        g_free(block->bmap);
        block->bmap = NULL;
        g_free(block->file_bmap);
        block->file_bmap = NULL;
    }
}

static void ram_save_cleanup(void *opaque)
{
    RAMState **rsp = opaque;

    if (!migrate_background_snapshot()) {
        if (global_dirty_tracking & GLOBAL_DIRTY_MIGRATION) {
            memory_global_dirty_log_stop(GLOBAL_DIRTY_MIGRATION);
        }
    }

    ram_bitmaps_destroy();

    xbzrle_cleanup();
    multifd_ram_save_cleanup();
    ram_state_cleanup(rsp);
}

static void ram_page_hint_reset(PageLocationHint *hint)
{
    hint->location.block = NULL;
    hint->location.offset = 0;
    hint->valid = false;
}

static void ram_state_reset(RAMState *rs)
{
    int i;

    for (i = 0; i < RAM_CHANNEL_MAX; i++) {
        rs->pss[i].last_sent_block = NULL;
    }

    rs->last_seen_block = NULL;
    rs->last_page = 0;
    rs->last_version = ram_list.version;
    rs->xbzrle_started = false;

    ram_page_hint_reset(&rs->page_hint);
}

#define MAX_WAIT 50

void ram_postcopy_migrated_memory_release(MigrationState *ms)
{
    struct RAMBlock *block;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        unsigned long *bitmap = block->bmap;
        unsigned long range = block->used_length >> TARGET_PAGE_BITS;
        unsigned long run_start = find_next_zero_bit(bitmap, range, 0);

        while (run_start < range) {
            unsigned long run_end = find_next_bit(bitmap, range, run_start + 1);
            ram_discard_range(block->idstr,
                              ((ram_addr_t)run_start) << TARGET_PAGE_BITS,
                              ((ram_addr_t)(run_end - run_start))
                                << TARGET_PAGE_BITS);
            run_start = find_next_zero_bit(bitmap, range, run_end + 1);
        }
    }
}

static void postcopy_send_discard_bm_ram(MigrationState *ms, RAMBlock *block)
{
    unsigned long end = block->used_length >> TARGET_PAGE_BITS;
    unsigned long current;
    unsigned long *bitmap = block->bmap;

    for (current = 0; current < end; ) {
        unsigned long one = find_next_bit(bitmap, end, current);
        unsigned long zero, discard_length;

        if (one >= end) {
            break;
        }

        zero = find_next_zero_bit(bitmap, end, one + 1);

        if (zero >= end) {
            discard_length = end - one;
        } else {
            discard_length = zero - one;
        }
        postcopy_discard_send_range(ms, one, discard_length);
        current = one + discard_length;
    }
}

static void postcopy_chunk_hostpages_pass(MigrationState *ms, RAMBlock *block);

static void postcopy_each_ram_send_discard(MigrationState *ms)
{
    struct RAMBlock *block;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        postcopy_discard_send_init(ms, block->idstr);

        postcopy_chunk_hostpages_pass(ms, block);

        postcopy_send_discard_bm_ram(ms, block);
        postcopy_discard_send_finish(ms);
    }
}

static void postcopy_chunk_hostpages_pass(MigrationState *ms, RAMBlock *block)
{
    RAMState *rs = ram_state;
    unsigned long *bitmap = block->bmap;
    unsigned int host_ratio = block->page_size / TARGET_PAGE_SIZE;
    unsigned long pages = block->used_length >> TARGET_PAGE_BITS;
    unsigned long run_start;

    if (block->page_size == TARGET_PAGE_SIZE) {
        return;
    }

    run_start = find_next_bit(bitmap, pages, 0);

    while (run_start < pages) {

        if (QEMU_IS_ALIGNED(run_start, host_ratio)) {
            run_start = find_next_zero_bit(bitmap, pages, run_start + 1);
        }

        if (!QEMU_IS_ALIGNED(run_start, host_ratio)) {
            unsigned long page;
            unsigned long fixup_start_addr = QEMU_ALIGN_DOWN(run_start,
                                                             host_ratio);
            run_start = QEMU_ALIGN_UP(run_start, host_ratio);

            for (page = fixup_start_addr;
                 page < fixup_start_addr + host_ratio; page++) {
                rs->migration_dirty_pages += !test_and_set_bit(page, bitmap);
            }
        }

        run_start = find_next_bit(bitmap, pages, run_start);
    }
}

void ram_postcopy_send_discard_bitmap(MigrationState *ms)
{
    RAMState *rs = ram_state;

    RCU_READ_LOCK_GUARD();

    migration_bitmap_sync(rs, false);

    rs->pss[RAM_CHANNEL_PRECOPY].last_sent_block = NULL;
    rs->last_seen_block = NULL;
    rs->last_page = 0;

    postcopy_each_ram_send_discard(ms);

    trace_ram_postcopy_send_discard_bitmap();
}

int ram_discard_range(const char *rbname, uint64_t start, size_t length)
{
    trace_ram_discard_range(rbname, start, length);

    RCU_READ_LOCK_GUARD();
    RAMBlock *rb = qemu_ram_block_by_name(rbname);

    if (!rb) {
        error_report("ram_discard_range: Failed to find block '%s'", rbname);
        return -1;
    }

    if (rb->receivedmap) {
        bitmap_clear(rb->receivedmap, start >> qemu_target_page_bits(),
                     length >> qemu_target_page_bits());
    }

    return ram_block_discard_range(rb, start, length);
}

static bool xbzrle_init(Error **errp)
{
    if (!migrate_xbzrle()) {
        return true;
    }

    XBZRLE_cache_lock();

    XBZRLE.zero_target_page = g_try_malloc0(TARGET_PAGE_SIZE);
    if (!XBZRLE.zero_target_page) {
        error_setg(errp, "%s: Error allocating zero page", __func__);
        goto err_out;
    }

    XBZRLE.cache = cache_init(migrate_xbzrle_cache_size(),
                              TARGET_PAGE_SIZE, errp);
    if (!XBZRLE.cache) {
        goto free_zero_page;
    }

    XBZRLE.encoded_buf = g_try_malloc0(TARGET_PAGE_SIZE);
    if (!XBZRLE.encoded_buf) {
        error_setg(errp, "%s: Error allocating encoded_buf", __func__);
        goto free_cache;
    }

    XBZRLE.current_buf = g_try_malloc(TARGET_PAGE_SIZE);
    if (!XBZRLE.current_buf) {
        error_setg(errp, "%s: Error allocating current_buf", __func__);
        goto free_encoded_buf;
    }

    XBZRLE_cache_unlock();
    return true;

free_encoded_buf:
    g_free(XBZRLE.encoded_buf);
    XBZRLE.encoded_buf = NULL;
free_cache:
    cache_fini(XBZRLE.cache);
    XBZRLE.cache = NULL;
free_zero_page:
    g_free(XBZRLE.zero_target_page);
    XBZRLE.zero_target_page = NULL;
err_out:
    XBZRLE_cache_unlock();
    return false;
}

static bool ram_state_init(RAMState **rsp, Error **errp)
{
    *rsp = g_try_new0(RAMState, 1);

    if (!*rsp) {
        error_setg(errp, "%s: Init ramstate fail", __func__);
        return false;
    }

    qemu_mutex_init(&(*rsp)->bitmap_mutex);
    qemu_mutex_init(&(*rsp)->src_page_req_mutex);
    QSIMPLEQ_INIT(&(*rsp)->src_page_requests);
    (*rsp)->ram_bytes_total = ram_bytes_total();

    (*rsp)->migration_dirty_pages = (*rsp)->ram_bytes_total >> TARGET_PAGE_BITS;
    ram_state_reset(*rsp);

    return true;
}

static void ram_list_init_bitmaps(void)
{
    MigrationState *ms = migrate_get_current();
    RAMBlock *block;
    unsigned long pages;
    uint8_t shift;

    if (ram_bytes_total()) {
        shift = ms->clear_bitmap_shift;
        if (shift > CLEAR_BITMAP_SHIFT_MAX) {
            error_report("clear_bitmap_shift (%u) too big, using "
                         "max value (%u)", shift, CLEAR_BITMAP_SHIFT_MAX);
            shift = CLEAR_BITMAP_SHIFT_MAX;
        } else if (shift < CLEAR_BITMAP_SHIFT_MIN) {
            error_report("clear_bitmap_shift (%u) too small, using "
                         "min value (%u)", shift, CLEAR_BITMAP_SHIFT_MIN);
            shift = CLEAR_BITMAP_SHIFT_MIN;
        }

        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            pages = block->max_length >> TARGET_PAGE_BITS;
            block->bmap = bitmap_new(pages);
            bitmap_set(block->bmap, 0, pages);
            if (migrate_mapped_ram()) {
                block->file_bmap = bitmap_new(pages);
            }
            block->clear_bmap_shift = shift;
            block->clear_bmap = bitmap_new(clear_bmap_size(pages, shift));
        }
    }
}

static void migration_bitmap_clear_discarded_pages(RAMState *rs)
{
    unsigned long pages;
    RAMBlock *rb;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
            pages = ramblock_dirty_bitmap_clear_discarded_pages(rb);
            rs->migration_dirty_pages -= pages;
    }
}

static bool ram_init_bitmaps(RAMState *rs, Error **errp)
{
    bool ret = true;

    qemu_mutex_lock_ramlist();

    WITH_RCU_READ_LOCK_GUARD() {
        ram_list_init_bitmaps();
        if (!migrate_background_snapshot()) {
            ret = memory_global_dirty_log_start(GLOBAL_DIRTY_MIGRATION, errp);
            if (!ret) {
                goto out_unlock;
            }
            migration_bitmap_sync_precopy(false);
        }
    }
out_unlock:
    qemu_mutex_unlock_ramlist();

    if (!ret) {
        ram_bitmaps_destroy();
        return false;
    }

    migration_bitmap_clear_discarded_pages(rs);
    return true;
}

static int ram_init_all(RAMState **rsp, Error **errp)
{
    if (!ram_state_init(rsp, errp)) {
        return -1;
    }

    if (!xbzrle_init(errp)) {
        ram_state_cleanup(rsp);
        return -1;
    }

    if (!ram_init_bitmaps(*rsp, errp)) {
        return -1;
    }

    return 0;
}

static void ram_state_resume_prepare(RAMState *rs, QEMUFile *out)
{
    RAMBlock *block;
    uint64_t pages = 0;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        pages += bitmap_count_one(block->bmap,
                                  block->used_length >> TARGET_PAGE_BITS);
    }

    rs->migration_dirty_pages = pages;

    ram_state_reset(rs);

    rs->pss[RAM_CHANNEL_PRECOPY].pss_channel = out;

    trace_ram_state_resume_prepare(pages);
}

void qemu_guest_free_page_hint(void *addr, size_t len)
{
    RAMBlock *block;
    ram_addr_t offset;
    size_t used_len, start, npages;

    if (!migration_is_running()) {
        return;
    }

    for (; len > 0; len -= used_len, addr += used_len) {
        block = qemu_ram_block_from_host(addr, false, &offset);
        if (unlikely(!block || offset >= block->used_length)) {
            error_report_once("%s unexpected error", __func__);
            return;
        }

        if (len <= block->used_length - offset) {
            used_len = len;
        } else {
            used_len = block->used_length - offset;
        }

        start = offset >> TARGET_PAGE_BITS;
        npages = used_len >> TARGET_PAGE_BITS;

        qemu_mutex_lock(&ram_state->bitmap_mutex);
        migration_clear_memory_region_dirty_bitmap_range(block, start, npages);
        ram_state->migration_dirty_pages -=
                      bitmap_count_one_with_offset(block->bmap, start, npages);
        bitmap_clear(block->bmap, start, npages);
        qemu_mutex_unlock(&ram_state->bitmap_mutex);
    }
}

#define MAPPED_RAM_HDR_VERSION 1
struct MappedRamHeader {
    uint32_t version;
    uint64_t page_size;
    uint64_t bitmap_offset;
    uint64_t pages_offset;
} QEMU_PACKED;
typedef struct MappedRamHeader MappedRamHeader;

static void mapped_ram_setup_ramblock(QEMUFile *file, RAMBlock *block)
{
    g_autofree MappedRamHeader *header = NULL;
    size_t header_size, bitmap_size;
    long num_pages;

    header = g_new0(MappedRamHeader, 1);
    header_size = sizeof(MappedRamHeader);

    num_pages = block->used_length >> TARGET_PAGE_BITS;
    bitmap_size = BITS_TO_LONGS(num_pages) * sizeof(unsigned long);

    block->bitmap_offset = qemu_get_offset(file) + header_size;
    block->pages_offset = ROUND_UP(block->bitmap_offset +
                                   bitmap_size,
                                   MAPPED_RAM_FILE_OFFSET_ALIGNMENT);

    header->version = cpu_to_be32(MAPPED_RAM_HDR_VERSION);
    header->page_size = cpu_to_be64(TARGET_PAGE_SIZE);
    header->bitmap_offset = cpu_to_be64(block->bitmap_offset);
    header->pages_offset = cpu_to_be64(block->pages_offset);

    qemu_put_buffer(file, (uint8_t *) header, header_size);

    qemu_set_offset(file, block->pages_offset + block->used_length, SEEK_SET);
}

static bool mapped_ram_read_header(QEMUFile *file, MappedRamHeader *header,
                                   Error **errp)
{
    size_t ret, header_size = sizeof(MappedRamHeader);

    ret = qemu_get_buffer(file, (uint8_t *)header, header_size);
    if (ret != header_size) {
        error_setg(errp, "Could not read whole mapped-ram migration header "
                   "(expected %zd, got %zd bytes)", header_size, ret);
        return false;
    }

    header->version = be32_to_cpu(header->version);

    if (header->version > MAPPED_RAM_HDR_VERSION) {
        error_setg(errp, "Migration mapped-ram capability version not "
                   "supported (expected <= %d, got %d)", MAPPED_RAM_HDR_VERSION,
                   header->version);
        return false;
    }

    header->page_size = be64_to_cpu(header->page_size);
    header->bitmap_offset = be64_to_cpu(header->bitmap_offset);
    header->pages_offset = be64_to_cpu(header->pages_offset);

    return true;
}

static int ram_save_setup(QEMUFile *f, void *opaque, Error **errp)
{
    RAMState **rsp = opaque;
    RAMBlock *block;
    int ret, max_hg_page_size;

    if (!migration_in_colo_state()) {
        if (ram_init_all(rsp, errp) != 0) {
            return -1;
        }
    }
    (*rsp)->pss[RAM_CHANNEL_PRECOPY].pss_channel = f;

    max_hg_page_size = MAX(qemu_real_host_page_size(), TARGET_PAGE_SIZE);

    WITH_RCU_READ_LOCK_GUARD() {
        qemu_put_be64(f, ram_bytes_total_with_ignored()
                         | RAM_SAVE_FLAG_MEM_SIZE);

        RAMBLOCK_FOREACH_MIGRATABLE(block) {
            qemu_put_byte(f, strlen(block->idstr));
            qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
            qemu_put_be64(f, block->used_length);
            if (migrate_postcopy_ram() &&
                block->page_size != max_hg_page_size) {
                qemu_put_be64(f, block->page_size);
            }
            if (migrate_ignore_shared()) {
                qemu_put_be64(f, block->mr->addr);
            }

            if (migrate_mapped_ram()) {
                mapped_ram_setup_ramblock(f, block);
            }
        }
    }

    ret = rdma_registration_start(f, RAM_CONTROL_SETUP);
    if (ret < 0) {
        error_setg(errp, "%s: failed to start RDMA registration", __func__);
        qemu_file_set_error(f, ret);
        return ret;
    }

    ret = rdma_registration_stop(f, RAM_CONTROL_SETUP);
    if (ret < 0) {
        error_setg(errp, "%s: failed to stop RDMA registration", __func__);
        qemu_file_set_error(f, ret);
        return ret;
    }

    if (migrate_multifd()) {
        multifd_ram_save_setup();
    }

    bql_unlock();
    ret = multifd_ram_flush_and_sync(f);
    bql_lock();
    if (ret < 0) {
        error_setg(errp, "%s: multifd synchronization failed", __func__);
        return ret;
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    ret = qemu_fflush(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "%s failed", __func__);
    }
    return ret;
}

static void ram_save_file_bmap(QEMUFile *f)
{
    RAMBlock *block;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        long num_pages = block->used_length >> TARGET_PAGE_BITS;
        long bitmap_size = BITS_TO_LONGS(num_pages) * sizeof(unsigned long);

        qemu_put_buffer_at(f, (uint8_t *)block->file_bmap, bitmap_size,
                           block->bitmap_offset);
        ram_transferred_add(bitmap_size);

        g_free(block->file_bmap);
        block->file_bmap = NULL;
    }
}

void ramblock_set_file_bmap_atomic(RAMBlock *block, ram_addr_t offset, bool set)
{
    if (set) {
        set_bit_atomic(offset >> TARGET_PAGE_BITS, block->file_bmap);
    } else {
        clear_bit_atomic(offset >> TARGET_PAGE_BITS, block->file_bmap);
    }
}

static int ram_save_iterate(QEMUFile *f, void *opaque)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;
    int ret = 0;
    int i;
    int64_t t0;
    int done = 0;

    WITH_QEMU_LOCK_GUARD(&rs->bitmap_mutex) {
        WITH_RCU_READ_LOCK_GUARD() {
            if (ram_list.version != rs->last_version) {
                ram_state_reset(rs);
            }

            smp_rmb();

            ret = rdma_registration_start(f, RAM_CONTROL_ROUND);
            if (ret < 0) {
                qemu_file_set_error(f, ret);
                goto out;
            }

            t0 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            i = 0;
            while ((ret = migration_rate_exceeded(f)) == 0 ||
                   postcopy_has_request(rs)) {
                int pages;

                if (qemu_file_get_error(f)) {
                    break;
                }

                pages = ram_find_and_save_block(rs);
                if (pages == 0) {
                    done = 1;
                    break;
                }

                if (pages < 0) {
                    qemu_file_set_error(f, pages);
                    break;
                }

                rs->target_page_count += pages;

                if ((i & 63) == 0) {
                    uint64_t t1 = (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - t0) /
                        1000000;
                    if (t1 > MAX_WAIT) {
                        trace_ram_save_iterate_big_wait(t1, i);
                        break;
                    }
                }
                i++;
            }
        }
    }

    ret = rdma_registration_stop(f, RAM_CONTROL_ROUND);
    if (ret < 0) {
        qemu_file_set_error(f, ret);
    }

out:
    if (ret >= 0 && migration_is_running()) {
        if (multifd_ram_sync_per_section()) {
            ret = multifd_ram_flush_and_sync(f);
            if (ret < 0) {
                return ret;
            }
        }

        qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
        ram_transferred_add(8);
        ret = qemu_fflush(f);
    }
    if (ret < 0) {
        return ret;
    }

    return done;
}

static int ram_save_complete(QEMUFile *f, void *opaque)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;
    int ret = 0;

    trace_ram_save_complete(rs->migration_dirty_pages, 0);

    rs->last_stage = !migration_in_colo_state();

    WITH_RCU_READ_LOCK_GUARD() {
        if (!migration_in_postcopy()) {
            migration_bitmap_sync_precopy(true);
        }

        ret = rdma_registration_start(f, RAM_CONTROL_FINISH);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return ret;
        }

        qemu_mutex_lock(&rs->bitmap_mutex);
        while (true) {
            int pages;

            pages = ram_find_and_save_block(rs);
            if (pages == 0) {
                break;
            }
            if (pages < 0) {
                qemu_mutex_unlock(&rs->bitmap_mutex);
                return pages;
            }
        }
        qemu_mutex_unlock(&rs->bitmap_mutex);

        ret = rdma_registration_stop(f, RAM_CONTROL_FINISH);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return ret;
        }
    }

    if (multifd_ram_sync_per_section()) {
        ret = multifd_ram_flush_and_sync(f);
        if (ret < 0) {
            return ret;
        }
    }

    if (migrate_mapped_ram()) {
        ram_save_file_bmap(f);

        if (qemu_file_get_error(f)) {
            Error *local_err = NULL;
            int err = qemu_file_get_error_obj(f, &local_err);

            error_reportf_err(local_err, "Failed to write bitmap to file: ");
            return -err;
        }
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    trace_ram_save_complete(rs->migration_dirty_pages, 1);

    return qemu_fflush(f);
}

static void ram_state_pending_estimate(void *opaque, uint64_t *must_precopy,
                                       uint64_t *can_postcopy)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;

    uint64_t remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;

    if (migrate_postcopy_ram()) {
        *can_postcopy += remaining_size;
    } else {
        *must_precopy += remaining_size;
    }
}

static void ram_state_pending_exact(void *opaque, uint64_t *must_precopy,
                                    uint64_t *can_postcopy)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;
    uint64_t remaining_size;

    if (!migration_in_postcopy()) {
        bql_lock();
        WITH_RCU_READ_LOCK_GUARD() {
            migration_bitmap_sync_precopy(false);
        }
        bql_unlock();
    }

    remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;

    if (migrate_postcopy_ram()) {
        *can_postcopy += remaining_size;
    } else {
        *must_precopy += remaining_size;
    }
}

static int load_xbzrle(QEMUFile *f, ram_addr_t addr, void *host)
{
    unsigned int xh_len;
    int xh_flags;
    uint8_t *loaded_data;

    xh_flags = qemu_get_byte(f);
    xh_len = qemu_get_be16(f);

    if (xh_flags != ENCODING_FLAG_XBZRLE) {
        error_report("Failed to load XBZRLE page - wrong compression!");
        return -1;
    }

    if (xh_len > TARGET_PAGE_SIZE) {
        error_report("Failed to load XBZRLE page - len overflow!");
        return -1;
    }
    loaded_data = XBZRLE.decoded_buf;
    qemu_get_buffer_in_place(f, &loaded_data, xh_len);

    if (xbzrle_decode_buffer(loaded_data, xh_len, host,
                             TARGET_PAGE_SIZE) == -1) {
        error_report("Failed to load XBZRLE page - decode error!");
        return -1;
    }

    return 0;
}

static inline RAMBlock *ram_block_from_stream(MigrationIncomingState *mis,
                                              QEMUFile *f, int flags,
                                              int channel)
{
    RAMBlock *block = mis->last_recv_block[channel];
    char id[256];
    uint8_t len;

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!block) {
            error_report("Ack, bad migration stream!");
            return NULL;
        }
        return block;
    }

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)id, len);
    id[len] = 0;

    block = qemu_ram_block_by_name(id);
    if (!block) {
        error_report("Can't find block %s", id);
        return NULL;
    }

    if (migrate_ram_is_ignored(block)) {
        error_report("block %s should not be migrated !", id);
        return NULL;
    }

    mis->last_recv_block[channel] = block;

    return block;
}

static inline void *host_from_ram_block_offset(RAMBlock *block,
                                               ram_addr_t offset)
{
    if (!offset_in_ramblock(block, offset)) {
        return NULL;
    }

    return block->host + offset;
}

static void *host_page_from_ram_block_offset(RAMBlock *block,
                                             ram_addr_t offset)
{
    return (void *)QEMU_ALIGN_DOWN((uintptr_t)(block->host + offset),
                                   block->page_size);
}

static ram_addr_t host_page_offset_from_ram_block_offset(RAMBlock *block,
                                                         ram_addr_t offset)
{
    return ((uintptr_t)block->host + offset) & (block->page_size - 1);
}

void colo_record_bitmap(RAMBlock *block, ram_addr_t *normal, uint32_t pages)
{
    qemu_mutex_lock(&ram_state->bitmap_mutex);
    for (int i = 0; i < pages; i++) {
        ram_addr_t offset = normal[i];
        ram_state->migration_dirty_pages += !test_and_set_bit(
                                                offset >> TARGET_PAGE_BITS,
                                                block->bmap);
    }
    qemu_mutex_unlock(&ram_state->bitmap_mutex);
}

static inline void *colo_cache_from_block_offset(RAMBlock *block,
                             ram_addr_t offset, bool record_bitmap)
{
    if (!offset_in_ramblock(block, offset)) {
        return NULL;
    }
    if (!block->colo_cache) {
        error_report("%s: colo_cache is NULL in block :%s",
                     __func__, block->idstr);
        return NULL;
    }

    if (record_bitmap) {
        colo_record_bitmap(block, &offset, 1);
    }
    return block->colo_cache + offset;
}

void ram_handle_zero(void *host, uint64_t size)
{
    if (!buffer_is_zero(host, size)) {
        memset(host, 0, size);
    }
}

static void colo_init_ram_state(void)
{
    Error *local_err = NULL;

    if (!ram_state_init(&ram_state, &local_err)) {
        error_report_err(local_err);
    }
}

int colo_init_ram_cache(Error **errp)
{
    RAMBlock *block;

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            block->colo_cache = qemu_anon_ram_alloc(block->used_length,
                                                    NULL, false, false);
            if (!block->colo_cache) {
                error_setg(errp, "Can't alloc memory for COLO cache of "
                           "block %s, size 0x" RAM_ADDR_FMT,
                           block->idstr, block->used_length);
                RAMBLOCK_FOREACH_NOT_IGNORED(block) {
                    if (block->colo_cache) {
                        qemu_anon_ram_free(block->colo_cache, block->used_length);
                        block->colo_cache = NULL;
                    }
                }
                return -1;
            }
            if (!machine_dump_guest_core(current_machine)) {
                qemu_madvise(block->colo_cache, block->used_length,
                             QEMU_MADV_DONTDUMP);
            }
        }
    }

    if (ram_bytes_total()) {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            unsigned long pages = block->max_length >> TARGET_PAGE_BITS;
            block->bmap = bitmap_new(pages);
        }
    }

    colo_init_ram_state();
    return 0;
}

void colo_incoming_start_dirty_log(void)
{
    RAMBlock *block = NULL;
    Error *local_err = NULL;

    bql_lock();
    qemu_mutex_lock_ramlist();

    memory_global_dirty_log_sync(false);
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(ram_state, block);
            bitmap_zero(block->bmap, block->max_length >> TARGET_PAGE_BITS);
        }
        if (!memory_global_dirty_log_start(GLOBAL_DIRTY_MIGRATION,
                                           &local_err)) {
            error_report_err(local_err);
        }
    }
    ram_state->migration_dirty_pages = 0;
    qemu_mutex_unlock_ramlist();
    bql_unlock();
}

void colo_release_ram_cache(void)
{
    RAMBlock *block;

    memory_global_dirty_log_stop(GLOBAL_DIRTY_MIGRATION);
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        g_free(block->bmap);
        block->bmap = NULL;
    }

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            if (block->colo_cache) {
                qemu_anon_ram_free(block->colo_cache, block->used_length);
                block->colo_cache = NULL;
            }
        }
    }
    ram_state_cleanup(&ram_state);
}

static int ram_load_setup(QEMUFile *f, void *opaque, Error **errp)
{
    xbzrle_load_setup();
    ramblock_recv_map_init();

    return 0;
}

static int ram_load_cleanup(void *opaque)
{
    RAMBlock *rb;

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        if (memory_region_is_nonvolatile(rb->mr)) {
            qemu_ram_block_writeback(rb);
        }
    }

    xbzrle_load_cleanup();

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        g_free(rb->receivedmap);
        rb->receivedmap = NULL;
    }

    return 0;
}

int ram_postcopy_incoming_init(MigrationIncomingState *mis, Error **errp)
{
    return postcopy_ram_incoming_init(mis, errp);
}

int ram_load_postcopy(QEMUFile *f, int channel)
{
    int flags = 0, ret = 0;
    bool place_needed = false;
    bool matches_target_page_size = false;
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyTmpPage *tmp_page = &mis->postcopy_tmp_pages[channel];

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr;
        void *page_buffer = NULL;
        void *place_source = NULL;
        RAMBlock *block = NULL;
        uint8_t ch;

        addr = qemu_get_be64(f);

        ret = qemu_file_get_error(f);
        if (ret) {
            break;
        }

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        trace_ram_load_postcopy_loop(channel, (uint64_t)addr, flags);
        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE)) {
            block = ram_block_from_stream(mis, f, flags, channel);
            if (!block) {
                ret = -EINVAL;
                break;
            }

            if (!block->host || addr >= block->postcopy_length) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            tmp_page->target_pages++;
            matches_target_page_size = block->page_size == TARGET_PAGE_SIZE;
            page_buffer = tmp_page->tmp_huge_page +
                          host_page_offset_from_ram_block_offset(block, addr);
            if (tmp_page->target_pages == 1) {
                tmp_page->host_addr =
                    host_page_from_ram_block_offset(block, addr);
            } else if (tmp_page->host_addr !=
                       host_page_from_ram_block_offset(block, addr)) {
                error_report("Non-same host page detected on channel %d: "
                             "Target host page %p, received host page %p "
                             "(rb %s offset 0x"RAM_ADDR_FMT" target_pages %d)",
                             channel, tmp_page->host_addr,
                             host_page_from_ram_block_offset(block, addr),
                             block->idstr, addr, tmp_page->target_pages);
                ret = -EINVAL;
                break;
            }

            if (tmp_page->target_pages ==
                (block->page_size / TARGET_PAGE_SIZE)) {
                place_needed = true;
            }
            place_source = tmp_page->tmp_huge_page;
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            if (ch != 0) {
                error_report("Found a zero page with value %d", ch);
                ret = -EINVAL;
                break;
            }
            if (!matches_target_page_size) {
                memset(page_buffer, ch, TARGET_PAGE_SIZE);
            }
            break;

        case RAM_SAVE_FLAG_PAGE:
            tmp_page->all_zero = false;
            if (!matches_target_page_size) {
                qemu_get_buffer(f, page_buffer, TARGET_PAGE_SIZE);
            } else {
                qemu_get_buffer_in_place(f, (uint8_t **)&place_source,
                                         TARGET_PAGE_SIZE);
            }
            break;
        case RAM_SAVE_FLAG_EOS:
            break;
        default:
            error_report("Unknown combination of migration flags: 0x%x"
                         " (postcopy mode)", flags);
            ret = -EINVAL;
            break;
        }

        if (!ret && qemu_file_get_error(f)) {
            ret = qemu_file_get_error(f);
        }

        if (!ret && place_needed) {
            if (tmp_page->all_zero) {
                ret = postcopy_place_page_zero(mis, tmp_page->host_addr, block);
            } else {
                ret = postcopy_place_page(mis, tmp_page->host_addr,
                                          place_source, block);
            }
            place_needed = false;
            postcopy_temp_page_reset(tmp_page);
        }
    }

    return ret;
}

static bool postcopy_is_running(void)
{
    PostcopyState ps = postcopy_state_get();
    return ps >= POSTCOPY_INCOMING_LISTENING && ps < POSTCOPY_INCOMING_END;
}

void colo_flush_ram_cache(void)
{
    RAMBlock *block = NULL;
    void *dst_host;
    void *src_host;
    unsigned long offset = 0;

    memory_global_dirty_log_sync(false);
    qemu_mutex_lock(&ram_state->bitmap_mutex);
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(ram_state, block);
        }
    }

    trace_colo_flush_ram_cache_begin(ram_state->migration_dirty_pages);
    WITH_RCU_READ_LOCK_GUARD() {
        block = QLIST_FIRST_RCU(&ram_list.blocks);

        while (block) {
            unsigned long num = 0;

            offset = colo_bitmap_find_dirty(ram_state, block, offset, &num);
            if (!offset_in_ramblock(block,
                                    ((ram_addr_t)offset) << TARGET_PAGE_BITS)) {
                offset = 0;
                num = 0;
                block = QLIST_NEXT_RCU(block, next);
            } else {
                unsigned long i = 0;

                for (i = 0; i < num; i++) {
                    migration_bitmap_clear_dirty(ram_state, block, offset + i);
                }
                dst_host = block->host
                         + (((ram_addr_t)offset) << TARGET_PAGE_BITS);
                src_host = block->colo_cache
                         + (((ram_addr_t)offset) << TARGET_PAGE_BITS);
                memcpy(dst_host, src_host, TARGET_PAGE_SIZE * num);
                offset += num;
            }
        }
    }
    qemu_mutex_unlock(&ram_state->bitmap_mutex);
    trace_colo_flush_ram_cache_end();
}

static size_t ram_load_multifd_pages(void *host_addr, size_t size,
                                     uint64_t offset)
{
    MultiFDRecvData *data = multifd_get_recv_data();

    data->opaque = host_addr;
    data->file_offset = offset;
    data->size = size;

    if (!multifd_recv()) {
        return 0;
    }

    return size;
}

static bool handle_zero_mapped_ram(RAMBlock *block, unsigned long from_bit_idx,
                                   unsigned long to_bit_idx, Error **errp)
{
    ERRP_GUARD();
    ram_addr_t offset;
    size_t size;
    void *host;

    if (!runstate_check(RUN_STATE_RESTORE_VM)) {
        return true;
    }

    if (from_bit_idx >= to_bit_idx) {
        return true;
    }

    size = TARGET_PAGE_SIZE * (to_bit_idx - from_bit_idx);
    offset = from_bit_idx << TARGET_PAGE_BITS;
    host = host_from_ram_block_offset(block, offset);
    if (!host) {
        error_setg(errp, "zero page outside of ramblock %s range",
                   block->idstr);
        return false;
    }
    ram_handle_zero(host, size);

    return true;
}

static bool read_ramblock_mapped_ram(QEMUFile *f, RAMBlock *block,
                                     long num_pages, unsigned long *bitmap,
                                     Error **errp)
{
    ERRP_GUARD();
    unsigned long set_bit_idx, clear_bit_idx = 0;
    ram_addr_t offset;
    void *host;
    size_t read, unread, size;

    for (set_bit_idx = find_first_bit(bitmap, num_pages);
         set_bit_idx < num_pages;
         set_bit_idx = find_next_bit(bitmap, num_pages, clear_bit_idx + 1)) {

        if (!handle_zero_mapped_ram(block, clear_bit_idx, set_bit_idx, errp)) {
            return false;
        }

        clear_bit_idx = find_next_zero_bit(bitmap, num_pages, set_bit_idx + 1);

        unread = TARGET_PAGE_SIZE * (clear_bit_idx - set_bit_idx);
        offset = set_bit_idx << TARGET_PAGE_BITS;

        while (unread > 0) {
            host = host_from_ram_block_offset(block, offset);
            if (!host) {
                error_setg(errp, "page outside of ramblock %s range",
                           block->idstr);
                return false;
            }

            size = MIN(unread, MAPPED_RAM_LOAD_BUF_SIZE);

            if (migrate_multifd()) {
                read = ram_load_multifd_pages(host, size,
                                              block->pages_offset + offset);
            } else {
                read = qemu_get_buffer_at(f, host, size,
                                          block->pages_offset + offset);
            }

            if (!read) {
                goto err;
            }
            offset += read;
            unread -= read;
        }
    }

    if (!handle_zero_mapped_ram(block, clear_bit_idx, num_pages, errp)) {
        return false;
    }

    return true;

err:
    qemu_file_get_error_obj(f, errp);
    error_prepend(errp, "(%s) failed to read page " RAM_ADDR_FMT
                  "from file offset %" PRIx64 ": ", block->idstr, offset,
                  block->pages_offset + offset);
    return false;
}

static void parse_ramblock_mapped_ram(QEMUFile *f, RAMBlock *block,
                                      ram_addr_t length, Error **errp)
{
    g_autofree unsigned long *bitmap = NULL;
    MappedRamHeader header;
    size_t bitmap_size;
    long num_pages;

    if (!mapped_ram_read_header(f, &header, errp)) {
        return;
    }

    block->pages_offset = header.pages_offset;

    if (!QEMU_IS_ALIGNED(block->pages_offset, TARGET_PAGE_SIZE)) {
        error_setg(errp,
                   "Error reading ramblock %s pages, region has bad alignment",
                   block->idstr);
        return;
    }

    num_pages = length / header.page_size;
    bitmap_size = BITS_TO_LONGS(num_pages) * sizeof(unsigned long);

    bitmap = g_malloc0(bitmap_size);
    if (qemu_get_buffer_at(f, (uint8_t *)bitmap, bitmap_size,
                           header.bitmap_offset) != bitmap_size) {
        error_setg(errp, "Error reading dirty bitmap");
        return;
    }

    if (!read_ramblock_mapped_ram(f, block, num_pages, bitmap, errp)) {
        return;
    }

    qemu_set_offset(f, block->pages_offset + length, SEEK_SET);
}

static int parse_ramblock(QEMUFile *f, RAMBlock *block, ram_addr_t length)
{
    int ret = 0;
    bool postcopy_advised = migration_incoming_postcopy_advised();
    int max_hg_page_size;
    Error *local_err = NULL;

    assert(block);

    if (migrate_mapped_ram()) {
        parse_ramblock_mapped_ram(f, block, length, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -EINVAL;
        }
        return 0;
    }

    if (!qemu_ram_is_migratable(block)) {
        error_report("block %s should not be migrated !", block->idstr);
        return -EINVAL;
    }

    if (length != block->used_length) {
        ret = qemu_ram_resize(block, length, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return ret;
        }
    }

    max_hg_page_size = MAX(qemu_real_host_page_size(), TARGET_PAGE_SIZE);

    if (postcopy_advised && migrate_postcopy_ram() &&
        block->page_size != max_hg_page_size) {
        uint64_t remote_page_size = qemu_get_be64(f);
        if (remote_page_size != block->page_size) {
            error_report("Mismatched RAM page size %s "
                         "(local) %zd != %" PRId64, block->idstr,
                         block->page_size, remote_page_size);
            return -EINVAL;
        }
    }
    if (migrate_ignore_shared()) {
        hwaddr addr = qemu_get_be64(f);
        if (migrate_ram_is_ignored(block) &&
            block->mr->addr != addr) {
            error_report("Mismatched GPAs for block %s "
                         "%" PRId64 "!= %" PRId64, block->idstr,
                         (uint64_t)addr, (uint64_t)block->mr->addr);
            return -EINVAL;
        }
    }
    ret = rdma_block_notification_handle(f, block->idstr);
    if (ret < 0) {
        qemu_file_set_error(f, ret);
    }

    return ret;
}

static int parse_ramblocks(QEMUFile *f, ram_addr_t total_ram_bytes)
{
    int ret = 0;

    while (!ret && total_ram_bytes) {
        RAMBlock *block;
        char id[256];
        ram_addr_t length;
        int len = qemu_get_byte(f);

        qemu_get_buffer(f, (uint8_t *)id, len);
        id[len] = 0;
        length = qemu_get_be64(f);

        block = qemu_ram_block_by_name(id);
        if (block) {
            ret = parse_ramblock(f, block, length);
        } else {
            error_report("Unknown ramblock \"%s\", cannot accept "
                         "migration", id);
            ret = -EINVAL;
        }
        total_ram_bytes -= length;
    }

    return ret;
}

static int ram_load_precopy(QEMUFile *f)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    int flags = 0, ret = 0, invalid_flags = 0, i = 0;

    if (migrate_mapped_ram()) {
        invalid_flags |= (RAM_SAVE_FLAG_HOOK | RAM_SAVE_FLAG_MULTIFD_FLUSH |
                          RAM_SAVE_FLAG_PAGE | RAM_SAVE_FLAG_XBZRLE |
                          RAM_SAVE_FLAG_ZERO);
    }

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr;
        void *host = NULL, *host_bak = NULL;
        uint8_t ch;

        if ((i & 32767) == 0 && qemu_in_coroutine()) {
            aio_co_schedule(qemu_get_current_aio_context(),
                            qemu_coroutine_self());
            qemu_coroutine_yield();
        }
        i++;

        addr = qemu_get_be64(f);
        ret = qemu_file_get_error(f);
        if (ret) {
            error_report("Getting RAM address failed");
            break;
        }

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        if (flags & invalid_flags) {
            error_report("Unexpected RAM flags: %d", flags & invalid_flags);

            ret = -EINVAL;
            break;
        }

        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE |
                     RAM_SAVE_FLAG_XBZRLE)) {
            RAMBlock *block = ram_block_from_stream(mis, f, flags,
                                                    RAM_CHANNEL_PRECOPY);

            host = host_from_ram_block_offset(block, addr);
            if (migration_incoming_colo_enabled()) {
                if (migration_incoming_in_colo_state()) {
                    host = colo_cache_from_block_offset(block, addr, true);
                } else {
                    host_bak = colo_cache_from_block_offset(block, addr, false);
                }
            }
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            if (!migration_incoming_in_colo_state()) {
                ramblock_recv_bitmap_set(block, host);
            }

            trace_ram_load_loop(block->idstr, (uint64_t)addr, flags, host);
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            ret = parse_ramblocks(f, addr);
            if (migrate_mapped_ram()) {
                multifd_recv_sync_main();
            }
            break;

        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            if (ch != 0) {
                error_report("Found a zero page with value %d", ch);
                ret = -EINVAL;
                break;
            }
            ram_handle_zero(host, TARGET_PAGE_SIZE);
            break;

        case RAM_SAVE_FLAG_PAGE:
            qemu_get_buffer(f, host, TARGET_PAGE_SIZE);
            break;

        case RAM_SAVE_FLAG_XBZRLE:
            if (load_xbzrle(f, addr, host) < 0) {
                error_report("Failed to decompress XBZRLE page at "
                             RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            break;
        case RAM_SAVE_FLAG_MULTIFD_FLUSH:
            multifd_recv_sync_main();
            break;
        case RAM_SAVE_FLAG_EOS:
            if (migrate_multifd() &&
                migrate_multifd_flush_after_each_section() &&
                !migrate_mapped_ram()) {
                multifd_recv_sync_main();
            }
            break;
        case RAM_SAVE_FLAG_HOOK:
            ret = rdma_registration_handle(f);
            if (ret < 0) {
                qemu_file_set_error(f, ret);
            }
            break;
        default:
            error_report("Unknown combination of migration flags: 0x%x", flags);
            ret = -EINVAL;
        }
        if (!ret) {
            ret = qemu_file_get_error(f);
        }
        if (!ret && host_bak) {
            memcpy(host_bak, host, TARGET_PAGE_SIZE);
        }
    }

    return ret;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    int ret = 0;
    static uint64_t seq_iter;
    bool postcopy_running = postcopy_is_running();

    seq_iter++;

    if (version_id != 4) {
        return -EINVAL;
    }

    trace_ram_load_start();
    WITH_RCU_READ_LOCK_GUARD() {
        if (postcopy_running) {
            ret = ram_load_postcopy(f, RAM_CHANNEL_PRECOPY);
        } else {
            ret = ram_load_precopy(f);
        }
    }
    trace_ram_load_complete(ret, seq_iter);

    return ret;
}

static bool ram_has_postcopy(void *opaque)
{
    RAMBlock *rb;
    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        if (ram_block_is_pmem(rb)) {
            info_report("Block: %s, host: %p is a nvdimm memory, postcopy"
                         "is not supported now!", rb->idstr, rb->host);
            return false;
        }
    }

    return migrate_postcopy_ram();
}

static int ram_dirty_bitmap_sync_all(MigrationState *s, RAMState *rs)
{
    RAMBlock *block;
    QEMUFile *file = s->to_dst_file;

    trace_ram_dirty_bitmap_sync_start();

    qatomic_set(&rs->postcopy_bmap_sync_requested, 0);
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        qemu_savevm_send_recv_bitmap(file, block->idstr);
        trace_ram_dirty_bitmap_request(block->idstr);
        qatomic_inc(&rs->postcopy_bmap_sync_requested);
    }

    trace_ram_dirty_bitmap_sync_wait();

    while (qatomic_read(&rs->postcopy_bmap_sync_requested)) {
        if (migration_rp_wait(s)) {
            return -1;
        }
    }

    trace_ram_dirty_bitmap_sync_complete();

    return 0;
}

bool ram_dirty_bitmap_reload(MigrationState *s, RAMBlock *block, Error **errp)
{
    QEMUFile *file = s->rp_state.from_dst_file;
    g_autofree unsigned long *le_bitmap = NULL;
    unsigned long nbits = block->used_length >> TARGET_PAGE_BITS;
    uint64_t local_size = DIV_ROUND_UP(nbits, 8);
    uint64_t size, end_mark;
    RAMState *rs = ram_state;

    trace_ram_dirty_bitmap_reload_begin(block->idstr);

    if (s->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_setg(errp, "Reload bitmap in incorrect state %s",
                   MigrationStatus_str(s->state));
        return false;
    }

    local_size = ROUND_UP(local_size, 8);

    le_bitmap = bitmap_new(nbits + BITS_PER_LONG);

    size = qemu_get_be64(file);

    if (size != local_size) {
        error_setg(errp, "ramblock '%s' bitmap size mismatch (0x%"PRIx64
                   " != 0x%"PRIx64")", block->idstr, size, local_size);
        return false;
    }

    size = qemu_get_buffer(file, (uint8_t *)le_bitmap, local_size);
    end_mark = qemu_get_be64(file);

    if (qemu_file_get_error(file) || size != local_size) {
        error_setg(errp, "read bitmap failed for ramblock '%s': "
                   "(size 0x%"PRIx64", got: 0x%"PRIx64")",
                   block->idstr, local_size, size);
        return false;
    }

    if (end_mark != RAMBLOCK_RECV_BITMAP_ENDING) {
        error_setg(errp, "ramblock '%s' end mark incorrect: 0x%"PRIx64,
                   block->idstr, end_mark);
        return false;
    }

    bitmap_from_le(block->bmap, le_bitmap, nbits);

    bitmap_complement(block->bmap, block->bmap, nbits);

    ramblock_dirty_bitmap_clear_discarded_pages(block);

    trace_ram_dirty_bitmap_reload_complete(block->idstr);

    qatomic_dec(&rs->postcopy_bmap_sync_requested);

    migration_rp_kick(s);

    return true;
}

static int ram_resume_prepare(MigrationState *s, void *opaque)
{
    RAMState *rs = *(RAMState **)opaque;
    int ret;

    ret = ram_dirty_bitmap_sync_all(s, rs);
    if (ret) {
        return ret;
    }

    ram_state_resume_prepare(rs, s->to_dst_file);

    return 0;
}

static bool ram_save_postcopy_prepare(QEMUFile *f, void *opaque, Error **errp)
{
    int ret;

    if (migrate_multifd()) {
        ret = multifd_ram_flush_and_sync(f);
        if (ret < 0) {
            error_setg(errp, "%s: multifd flush and sync failed", __func__);
            return false;
        }
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return true;
}

void postcopy_preempt_shutdown_file(MigrationState *s)
{
    qemu_put_be64(s->postcopy_qemufile_src, RAM_SAVE_FLAG_EOS);
    qemu_fflush(s->postcopy_qemufile_src);
}

static SaveVMHandlers savevm_ram_handlers = {
    .save_setup = ram_save_setup,
    .save_live_iterate = ram_save_iterate,
    .save_complete = ram_save_complete,
    .has_postcopy = ram_has_postcopy,
    .state_pending_exact = ram_state_pending_exact,
    .state_pending_estimate = ram_state_pending_estimate,
    .load_state = ram_load,
    .save_cleanup = ram_save_cleanup,
    .load_setup = ram_load_setup,
    .load_cleanup = ram_load_cleanup,
    .resume_prepare = ram_resume_prepare,
    .save_postcopy_prepare = ram_save_postcopy_prepare,
};

static void ram_mig_ram_block_resized(RAMBlockNotifier *n, void *host,
                                      size_t old_size, size_t new_size)
{
    PostcopyState ps = postcopy_state_get();
    ram_addr_t offset;
    RAMBlock *rb = qemu_ram_block_from_host(host, false, &offset);
    Error *err = NULL;

    if (!rb) {
        error_report("RAM block not found");
        return;
    }

    if (migrate_ram_is_ignored(rb)) {
        return;
    }

    if (migration_is_running()) {
        error_setg(&err, "RAM block '%s' resized during precopy.", rb->idstr);
        migrate_set_error(migrate_get_current(), err);
        error_free(err);

        migration_cancel();
    }

    switch (ps) {
    case POSTCOPY_INCOMING_ADVISE:
        if (old_size < new_size) {
            if (ram_discard_range(rb->idstr, old_size, new_size - old_size)) {
                error_report("RAM block '%s' discard of resized RAM failed",
                             rb->idstr);
            }
        }
        rb->postcopy_length = new_size;
        break;
    case POSTCOPY_INCOMING_NONE:
    case POSTCOPY_INCOMING_RUNNING:
    case POSTCOPY_INCOMING_END:
        break;
    default:
        error_report("RAM block '%s' resized during postcopy state: %d",
                     rb->idstr, ps);
        exit(-1);
    }
}

static RAMBlockNotifier ram_mig_ram_notifier = {
    .ram_block_resized = ram_mig_ram_block_resized,
};

void ram_mig_init(void)
{
    qemu_mutex_init(&XBZRLE.lock);
    register_savevm_live("ram", 0, 4, &savevm_ram_handlers, &ram_state);
    ram_block_notifier_add(&ram_mig_ram_notifier);
}
