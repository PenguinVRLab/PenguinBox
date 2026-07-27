/*
 * Postcopy migration for RAM
 *
 * Copyright 2013-2015 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Dave Gilbert  <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * Postcopy is a migration technique where the execution flips from the
 * source to the destination before all the data has been copied.
 */

#include "qemu/osdep.h"
#include "qemu/madvise.h"
#include "exec/target_page.h"
#include "migration.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "ram.h"
#include "qapi/error.h"
#include "qemu/notify.h"
#include "qemu/rcu.h"
#include "system/system.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/boards.h"
#include "system/ramblock.h"
#include "socket.h"
#include "yank_functions.h"
#include "tls.h"
#include "qemu/userfaultfd.h"
#include "qemu/mmap-alloc.h"
#include "options.h"

#define MAX_DISCARDS_PER_COMMAND 12

typedef struct PostcopyDiscardState {
    const char *ramblock_name;
    uint16_t cur_entry;
    uint64_t start_list[MAX_DISCARDS_PER_COMMAND];
    uint64_t length_list[MAX_DISCARDS_PER_COMMAND];
    unsigned int nsentwords;
    unsigned int nsentcmds;
} PostcopyDiscardState;

static NotifierWithReturnList postcopy_notifier_list;

void postcopy_infrastructure_init(void)
{
    notifier_with_return_list_init(&postcopy_notifier_list);
}

void postcopy_add_notifier(NotifierWithReturn *nn)
{
    notifier_with_return_list_add(&postcopy_notifier_list, nn);
}

void postcopy_remove_notifier(NotifierWithReturn *n)
{
    notifier_with_return_remove(n);
}

int postcopy_notify(enum PostcopyNotifyReason reason, Error **errp)
{
    struct PostcopyNotifyData pnd;
    pnd.reason = reason;

    return notifier_with_return_list_notify(&postcopy_notifier_list,
                                            &pnd, errp);
}

void postcopy_thread_create(MigrationIncomingState *mis,
                            QemuThread *thread, const char *name,
                            void *(*fn)(void *), int joinable)
{
    qemu_event_init(&mis->thread_sync_event, false);
    qemu_thread_create(thread, name, fn, mis, joinable);
    qemu_event_wait(&mis->thread_sync_event);
    qemu_event_destroy(&mis->thread_sync_event);
}

#if defined(__linux__)
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>

#define  BLOCKTIME_LATENCY_BUCKET_N  (24)

typedef struct PostcopyBlocktimeContext {
    uint64_t *vcpu_blocktime_total;
    uint64_t *vcpu_faults_count;
    uint8_t *vcpu_faults_current;
    GHashTable *vcpu_addr_hash;
    uint64_t latency_buckets[BLOCKTIME_LATENCY_BUCKET_N];
    uint64_t total_blocktime;
    uint64_t last_begin;
    int smp_cpus_down;

    GHashTable *tid_to_vcpu_hash;
    uint64_t non_vcpu_faults;
    uint64_t non_vcpu_blocktime_total;

    Notifier exit_notifier;
} PostcopyBlocktimeContext;

typedef struct {
    uint64_t fault_time;
    int cpu;
} BlocktimeVCPUEntry;

static BlocktimeVCPUEntry *
blocktime_vcpu_entry_alloc(int cpu, uint64_t fault_time)
{
    BlocktimeVCPUEntry *entry = g_new(BlocktimeVCPUEntry, 1);

    entry->fault_time = fault_time;
    entry->cpu = cpu;

    return entry;
}

static void blocktime_vcpu_list_free(gpointer data)
{
    g_list_free_full(data, g_free);
}

static void destroy_blocktime_context(struct PostcopyBlocktimeContext *ctx)
{
    g_hash_table_destroy(ctx->tid_to_vcpu_hash);
    g_hash_table_destroy(ctx->vcpu_addr_hash);
    g_free(ctx->vcpu_blocktime_total);
    g_free(ctx->vcpu_faults_count);
    g_free(ctx->vcpu_faults_current);
    g_free(ctx);
}

static void migration_exit_cb(Notifier *n, void *data)
{
    PostcopyBlocktimeContext *ctx = container_of(n, PostcopyBlocktimeContext,
                                                 exit_notifier);
    destroy_blocktime_context(ctx);
}

static GHashTable *blocktime_init_tid_to_vcpu_hash(void)
{
    GHashTable *table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, g_free);
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        int *value = g_new(int, 1);

        *value = cpu->cpu_index;
        g_hash_table_insert(table,
                            GUINT_TO_POINTER((uint32_t)cpu->thread_id),
                            value);
        trace_postcopy_blocktime_tid_cpu_map(cpu->cpu_index, cpu->thread_id);
    }

    return table;
}

static struct PostcopyBlocktimeContext *blocktime_context_new(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    PostcopyBlocktimeContext *ctx = g_new0(PostcopyBlocktimeContext, 1);

    memset(ctx->latency_buckets, 0, sizeof(ctx->latency_buckets));

    ctx->vcpu_blocktime_total = g_new0(uint64_t, smp_cpus);
    ctx->vcpu_faults_count = g_new0(uint64_t, smp_cpus);
    ctx->vcpu_faults_current = g_new0(uint8_t, smp_cpus);
    ctx->tid_to_vcpu_hash = blocktime_init_tid_to_vcpu_hash();

    ctx->vcpu_addr_hash = g_hash_table_new_full(g_direct_hash,
                                                g_direct_equal,
                                                NULL,
                                                blocktime_vcpu_list_free);

    ctx->exit_notifier.notify = migration_exit_cb;
    qemu_add_exit_notifier(&ctx->exit_notifier);

    return ctx;
}

void fill_destination_postcopy_migration_info(MigrationInfo *info)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *bc = mis->blocktime_ctx;
    MachineState *ms = MACHINE(qdev_get_machine());
    uint64_t latency_total = 0, faults = 0;
    uint32List *list_blocktime = NULL;
    uint64List *list_latency = NULL;
    uint64List *latency_buckets = NULL;
    int i;

    if (!bc) {
        return;
    }

    for (i = ms->smp.cpus - 1; i >= 0; i--) {
        uint64_t latency, total, count;

        QAPI_LIST_PREPEND(list_blocktime,
                          (uint32_t)(bc->vcpu_blocktime_total[i] / SCALE_MS));

        total = bc->vcpu_blocktime_total[i];
        latency_total += total;
        count = bc->vcpu_faults_count[i];
        faults += count;

        if (count) {
            latency = total / count;
        } else {
            latency = 0;
        }

        QAPI_LIST_PREPEND(list_latency, latency);
    }

    for (i = BLOCKTIME_LATENCY_BUCKET_N - 1; i >= 0; i--) {
        QAPI_LIST_PREPEND(latency_buckets, bc->latency_buckets[i]);
    }

    latency_total += bc->non_vcpu_blocktime_total;
    faults += bc->non_vcpu_faults;

    info->has_postcopy_non_vcpu_latency = true;
    info->postcopy_non_vcpu_latency = bc->non_vcpu_faults ?
        (bc->non_vcpu_blocktime_total / bc->non_vcpu_faults) : 0;
    info->has_postcopy_blocktime = true;
    info->postcopy_blocktime = (uint32_t)(bc->total_blocktime / SCALE_MS);
    info->has_postcopy_vcpu_blocktime = true;
    info->postcopy_vcpu_blocktime = list_blocktime;
    info->has_postcopy_latency = true;
    info->postcopy_latency = faults ? (latency_total / faults) : 0;
    info->has_postcopy_vcpu_latency = true;
    info->postcopy_vcpu_latency = list_latency;
    info->has_postcopy_latency_dist = true;
    info->postcopy_latency_dist = latency_buckets;
}

static uint64_t get_postcopy_total_blocktime(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *bc = mis->blocktime_ctx;

    if (!bc) {
        return 0;
    }

    return bc->total_blocktime;
}

static bool receive_ufd_features(uint64_t *features)
{
    struct uffdio_api api_struct = {0};
    int ufd;
    bool ret = true;

    ufd = uffd_open(O_CLOEXEC);
    if (ufd == -1) {
        error_report("%s: uffd_open() failed: %s", __func__, strerror(errno));
        return false;
    }

    api_struct.api = UFFD_API;
    api_struct.features = 0;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        error_report("%s: UFFDIO_API failed: %s", __func__,
                     strerror(errno));
        ret = false;
        goto release_ufd;
    }

    *features = api_struct.features;

release_ufd:
    close(ufd);
    return ret;
}

static bool request_ufd_features(int ufd, uint64_t features)
{
    struct uffdio_api api_struct = {0};
    uint64_t ioctl_mask;

    api_struct.api = UFFD_API;
    api_struct.features = features;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        error_report("%s failed: UFFDIO_API failed: %s", __func__,
                     strerror(errno));
        return false;
    }

    ioctl_mask = 1ULL << _UFFDIO_REGISTER |
                 1ULL << _UFFDIO_UNREGISTER;
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        error_report("Missing userfault features: %" PRIx64,
                     (uint64_t)(~api_struct.ioctls & ioctl_mask));
        return false;
    }

    return true;
}

static bool ufd_check_and_apply(int ufd, MigrationIncomingState *mis,
                                Error **errp)
{
    ERRP_GUARD();
    uint64_t asked_features = 0;
    static uint64_t supported_features;

    if (!supported_features) {
        if (!receive_ufd_features(&supported_features)) {
            error_setg(errp, "Userfault feature detection failed");
            return false;
        }
    }

#ifdef UFFD_FEATURE_THREAD_ID
    if (UFFD_FEATURE_THREAD_ID & supported_features) {
        asked_features |= UFFD_FEATURE_THREAD_ID;
    }
#endif

    if (!request_ufd_features(ufd, asked_features)) {
        error_setg(errp, "Failed features %" PRIu64, asked_features);
        return false;
    }

    if (qemu_real_host_page_size() != ram_pagesize_summary()) {
        bool have_hp = false;
#ifdef UFFD_FEATURE_MISSING_HUGETLBFS
        have_hp = supported_features & UFFD_FEATURE_MISSING_HUGETLBFS;
#endif
        if (!have_hp) {
            error_setg(errp,
                       "Userfault on this host does not support huge pages");
            return false;
        }
    }
    return true;
}

static int test_ramblock_postcopiable(RAMBlock *rb, Error **errp)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    size_t pagesize = qemu_ram_pagesize(rb);
    QemuFsType fs;

    if (length % pagesize) {
        error_setg(errp,
                   "Postcopy requires RAM blocks to be a page size multiple,"
                   " block %s is 0x" RAM_ADDR_FMT " bytes with a "
                   "page size of 0x%zx", block_name, length, pagesize);
        return 1;
    }

    if (rb->fd >= 0) {
        fs = qemu_fd_getfs(rb->fd);
        if (fs != QEMU_FS_TYPE_TMPFS && fs != QEMU_FS_TYPE_HUGETLBFS) {
            error_setg(errp,
                       "Host backend files need to be TMPFS or HUGETLBFS only");
            return 1;
        }
    }

    return 0;
}

bool postcopy_ram_supported_by_host(MigrationIncomingState *mis, Error **errp)
{
    ERRP_GUARD();
    long pagesize = qemu_real_host_page_size();
    int ufd = -1;
    bool ret = false;
    void *testarea = NULL;
    struct uffdio_register reg_struct;
    struct uffdio_range range_struct;
    uint64_t feature_mask;
    RAMBlock *block;

    if (qemu_target_page_size() > pagesize) {
        error_setg(errp, "Target page size bigger than host page size");
        goto out;
    }

    ufd = uffd_open(O_CLOEXEC);
    if (ufd == -1) {
        error_setg(errp, "Userfaultfd not available: %s", strerror(errno));
        goto out;
    }

    if (postcopy_notify(POSTCOPY_NOTIFY_PROBE, errp)) {
        goto out;
    }

    if (!ufd_check_and_apply(ufd, mis, errp)) {
        goto out;
    }

    RAMBLOCK_FOREACH(block) {
        if (test_ramblock_postcopiable(block, errp)) {
            goto out;
        }
    }

    if (munlockall()) {
        error_setg(errp, "munlockall() failed: %s", strerror(errno));
        goto out;
    }

    testarea = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE |
                                    MAP_ANONYMOUS, -1, 0);
    if (testarea == MAP_FAILED) {
        error_setg(errp, "Failed to map test area: %s", strerror(errno));
        goto out;
    }
    g_assert(QEMU_PTR_IS_ALIGNED(testarea, pagesize));

    reg_struct.range.start = (uintptr_t)testarea;
    reg_struct.range.len = pagesize;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    if (ioctl(ufd, UFFDIO_REGISTER, &reg_struct)) {
        error_setg(errp, "UFFDIO_REGISTER failed: %s", strerror(errno));
        goto out;
    }

    range_struct.start = (uintptr_t)testarea;
    range_struct.len = pagesize;
    if (ioctl(ufd, UFFDIO_UNREGISTER, &range_struct)) {
        error_setg(errp, "UFFDIO_UNREGISTER failed: %s", strerror(errno));
        goto out;
    }

    feature_mask = 1ULL << _UFFDIO_WAKE |
                   1ULL << _UFFDIO_COPY |
                   1ULL << _UFFDIO_ZEROPAGE;
    if ((reg_struct.ioctls & feature_mask) != feature_mask) {
        error_setg(errp, "Missing userfault map features: %" PRIx64,
                   (uint64_t)(~reg_struct.ioctls & feature_mask));
        goto out;
    }

    ret = true;
out:
    if (testarea) {
        munmap(testarea, pagesize);
    }
    if (ufd != -1) {
        close(ufd);
    }
    return ret;
}

static int init_range(RAMBlock *rb, void *opaque)
{
    Error **errp = opaque;
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    trace_postcopy_init_range(block_name, host_addr, offset, length);

    rb->postcopy_length = length;

    if (ram_discard_range(block_name, 0, length)) {
        error_setg(errp, "failed to discard RAM block %s len=%zu",
                   block_name, length);
        return -1;
    }

    return 0;
}

static int cleanup_range(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = rb->postcopy_length;
    MigrationIncomingState *mis = opaque;
    struct uffdio_range range_struct;
    trace_postcopy_cleanup_range(block_name, host_addr, offset, length);

    qemu_madvise(host_addr, length, QEMU_MADV_HUGEPAGE);

    range_struct.start = (uintptr_t)host_addr;
    range_struct.len = length;

    if (ioctl(mis->userfault_fd, UFFDIO_UNREGISTER, &range_struct)) {
        error_report("%s: userfault unregister %s", __func__, strerror(errno));

        return -1;
    }

    return 0;
}

int postcopy_ram_incoming_init(MigrationIncomingState *mis, Error **errp)
{
    if (foreach_not_ignored_block(init_range, errp)) {
        return -1;
    }

    return 0;
}

static void postcopy_temp_pages_cleanup(MigrationIncomingState *mis)
{
    int i;

    if (mis->postcopy_tmp_pages) {
        for (i = 0; i < mis->postcopy_channels; i++) {
            if (mis->postcopy_tmp_pages[i].tmp_huge_page) {
                munmap(mis->postcopy_tmp_pages[i].tmp_huge_page,
                       mis->largest_page_size);
                mis->postcopy_tmp_pages[i].tmp_huge_page = NULL;
            }
        }
        g_free(mis->postcopy_tmp_pages);
        mis->postcopy_tmp_pages = NULL;
    }

    if (mis->postcopy_tmp_zero_page) {
        munmap(mis->postcopy_tmp_zero_page, mis->largest_page_size);
        mis->postcopy_tmp_zero_page = NULL;
    }
}

int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    trace_postcopy_ram_incoming_cleanup_entry();

    if (mis->preempt_thread_status == PREEMPT_THREAD_CREATED) {
        mis->preempt_thread_status = PREEMPT_THREAD_QUIT;
        smp_mb();
        WITH_QEMU_LOCK_GUARD(&mis->page_request_mutex) {
            if (qatomic_read(&mis->page_requested_count)) {
                qemu_cond_wait(&mis->page_request_cond,
                               &mis->page_request_mutex);
            }
        }
        if (mis->postcopy_qemufile_dst) {
            qemu_file_shutdown(mis->postcopy_qemufile_dst);
        }
        qemu_thread_join(&mis->postcopy_prio_thread);
        mis->preempt_thread_status = PREEMPT_THREAD_NONE;
    }

    if (mis->have_fault_thread) {
        Error *local_err = NULL;

        qatomic_set(&mis->fault_thread_quit, 1);
        postcopy_fault_thread_notify(mis);
        trace_postcopy_ram_incoming_cleanup_join();
        qemu_thread_join(&mis->fault_thread);

        if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_END, &local_err)) {
            error_report_err(local_err);
            return -1;
        }

        if (foreach_not_ignored_block(cleanup_range, mis)) {
            return -1;
        }

        trace_postcopy_ram_incoming_cleanup_closeuf();
        close(mis->userfault_fd);
        close(mis->userfault_event_fd);
        mis->have_fault_thread = false;
    }

    if (should_mlock(mlock_state)) {
        if (os_mlock(is_mlock_on_fault(mlock_state)) < 0) {
            error_report("mlock: %s", strerror(errno));
        }
    }

    postcopy_temp_pages_cleanup(mis);

    trace_postcopy_ram_incoming_cleanup_blocktime(
            get_postcopy_total_blocktime());

    trace_postcopy_ram_incoming_cleanup_exit();
    return 0;
}

static int nhp_range(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = rb->postcopy_length;
    trace_postcopy_nhp_range(block_name, host_addr, offset, length);

    qemu_madvise(host_addr, length, QEMU_MADV_NOHUGEPAGE);

    return 0;
}

int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    if (foreach_not_ignored_block(nhp_range, mis)) {
        return -1;
    }

    postcopy_state_set(POSTCOPY_INCOMING_DISCARD);

    return 0;
}

static int ram_block_enable_notify(RAMBlock *rb, void *opaque)
{
    MigrationIncomingState *mis = opaque;
    struct uffdio_register reg_struct;

    reg_struct.range.start = (uintptr_t)qemu_ram_get_host_addr(rb);
    reg_struct.range.len = rb->postcopy_length;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    if (ioctl(mis->userfault_fd, UFFDIO_REGISTER, &reg_struct)) {
        error_report("%s userfault register: %s", __func__, strerror(errno));
        return -1;
    }
    if (!(reg_struct.ioctls & (1ULL << _UFFDIO_COPY))) {
        error_report("%s userfault: Region doesn't support COPY", __func__);
        return -1;
    }
    if (reg_struct.ioctls & (1ULL << _UFFDIO_ZEROPAGE)) {
        qemu_ram_set_uf_zeroable(rb);
    }

    return 0;
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    trace_postcopy_wake_shared(client_addr, qemu_ram_get_idstr(rb));
    return uffd_wakeup(pcfd->fd,
                       (void *)(uintptr_t)ROUND_DOWN(client_addr, pagesize),
                       pagesize);
}

static int postcopy_request_page(MigrationIncomingState *mis, RAMBlock *rb,
                                 ram_addr_t start, uint64_t haddr, uint32_t tid)
{
    void *aligned = (void *)(uintptr_t)ROUND_DOWN(haddr, qemu_ram_pagesize(rb));

    assert(QEMU_IS_ALIGNED(start, qemu_ram_pagesize(rb)));
    if (ramblock_page_is_discarded(rb, start)) {
        bool received = ramblock_recv_bitmap_test_byte_offset(rb, start);

        return received ? 0 : postcopy_place_page_zero(mis, aligned, rb);
    }

    return migrate_send_rp_req_pages(mis, rb, start, haddr, tid);
}

int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    uint64_t aligned_rbo = ROUND_DOWN(rb_offset, qemu_ram_pagesize(rb));
    MigrationIncomingState *mis = migration_incoming_get_current();

    trace_postcopy_request_shared_page(pcfd->idstr, qemu_ram_get_idstr(rb),
                                       rb_offset);
    if (ramblock_recv_bitmap_test_byte_offset(rb, aligned_rbo)) {
        trace_postcopy_request_shared_page_present(pcfd->idstr,
                                        qemu_ram_get_idstr(rb), rb_offset);
        return postcopy_wake_shared(pcfd, client_addr, rb);
    }
    postcopy_request_page(mis, rb, aligned_rbo, client_addr, 0);
    return 0;
}

static int blocktime_get_vcpu(PostcopyBlocktimeContext *ctx, uint32_t tid)
{
    int *found;

    found = g_hash_table_lookup(ctx->tid_to_vcpu_hash, GUINT_TO_POINTER(tid));
    if (!found) {
        return -1;
    }

    return *found;
}

static uint64_t get_current_ns(void)
{
    return (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

static void blocktime_fault_inject(PostcopyBlocktimeContext *ctx,
                                   uintptr_t addr, int cpu, uint64_t time)
{
    BlocktimeVCPUEntry *entry = blocktime_vcpu_entry_alloc(cpu, time);
    GHashTable *table = ctx->vcpu_addr_hash;
    gpointer key = (gpointer)addr;
    GList *head, *list;
    gboolean result;

    head = g_hash_table_lookup(table, key);
    if (head) {
        result = g_hash_table_steal(table, key);
        assert(result == TRUE);
    }

    list = g_list_prepend(head, entry);
    g_hash_table_insert(table, key, list);

    trace_postcopy_blocktime_begin(addr, time, cpu, !!head);
}

void mark_postcopy_blocktime_begin(uintptr_t addr, uint32_t ptid,
                                   RAMBlock *rb)
{
    int cpu;
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *dc = mis->blocktime_ctx;
    uint64_t current;

    if (!dc || ptid == 0) {
        return;
    }

    assert(!ramblock_recv_bitmap_test(rb, (void *)addr));

    current = get_current_ns();
    cpu = blocktime_get_vcpu(dc, ptid);

    if (cpu >= 0) {
        dc->vcpu_faults_count[cpu]++;

        if (dc->vcpu_faults_current[cpu]++ == 0) {
            dc->smp_cpus_down++;
            dc->last_begin = current;
        }

        assert(dc->vcpu_faults_current[cpu] <= 255);
    } else {
        dc->non_vcpu_faults++;
    }

    blocktime_fault_inject(dc, addr, cpu, current);
}

static void blocktime_latency_account(PostcopyBlocktimeContext *ctx,
                                      uint64_t time_us)
{
    int index = time_us ? (63 - clz64(time_us)) : 0;

    assert(index >= 0);

    if (index >= BLOCKTIME_LATENCY_BUCKET_N) {
        index = BLOCKTIME_LATENCY_BUCKET_N - 1;
    }

    ctx->latency_buckets[index]++;
}

typedef struct {
    PostcopyBlocktimeContext *ctx;
    uint64_t current;
    int affected_cpus;
    int affected_non_cpus;
} BlockTimeVCPUIter;

static void blocktime_cpu_list_iter_fn(gpointer data, gpointer user_data)
{
    BlockTimeVCPUIter *iter = user_data;
    PostcopyBlocktimeContext *ctx = iter->ctx;
    BlocktimeVCPUEntry *entry = data;
    uint64_t time_passed;
    int cpu = entry->cpu;

    assert(iter->current >= entry->fault_time);
    time_passed = iter->current - entry->fault_time;

    blocktime_latency_account(ctx, time_passed / SCALE_US);

    if (cpu >= 0) {
        if (--ctx->vcpu_faults_current[cpu] == 0) {
            ctx->vcpu_blocktime_total[cpu] += time_passed;
            iter->affected_cpus += 1;
        }
        trace_postcopy_blocktime_end_one(cpu, ctx->vcpu_faults_current[cpu]);
    } else {
        iter->affected_non_cpus++;
        ctx->non_vcpu_blocktime_total += time_passed;
        trace_postcopy_blocktime_end_one(-1, 0);
    }
}

static void mark_postcopy_blocktime_end(uintptr_t addr)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *dc = mis->blocktime_ctx;
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    BlockTimeVCPUIter iter = {
        .current = get_current_ns(),
        .affected_cpus = 0,
        .affected_non_cpus = 0,
        .ctx = dc,
    };
    gpointer key = (gpointer)addr;
    GHashTable *table;
    GList *list;

    if (!dc) {
        return;
    }

    table = dc->vcpu_addr_hash;
    list = g_hash_table_lookup(table, key);
    if (!list) {
        return;
    }

    g_list_foreach(list, blocktime_cpu_list_iter_fn, &iter);
    g_hash_table_remove(table, key);

    if (dc->smp_cpus_down == smp_cpus && iter.affected_cpus) {
        dc->total_blocktime += iter.current - dc->last_begin;
    }
    dc->smp_cpus_down -= iter.affected_cpus;

    trace_postcopy_blocktime_end(addr, iter.current, iter.affected_cpus,
                                 iter.affected_non_cpus);
}

static void postcopy_pause_fault_thread(MigrationIncomingState *mis)
{
    trace_postcopy_pause_fault_thread();
    qemu_sem_wait(&mis->postcopy_pause_sem_fault);
    trace_postcopy_pause_fault_thread_continued();
}

static void *postcopy_ram_fault_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    struct uffd_msg msg;
    int ret;
    size_t index;
    RAMBlock *rb = NULL;

    trace_postcopy_ram_fault_thread_entry();
    rcu_register_thread();
    mis->last_rb = NULL;
    qemu_event_set(&mis->thread_sync_event);

    struct pollfd *pfd;
    size_t pfd_len = 2 + mis->postcopy_remote_fds->len;

    pfd = g_new0(struct pollfd, pfd_len);

    pfd[0].fd = mis->userfault_fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = mis->userfault_event_fd;
    pfd[1].events = POLLIN;
    trace_postcopy_ram_fault_thread_fds_core(pfd[0].fd, pfd[1].fd);
    for (index = 0; index < mis->postcopy_remote_fds->len; index++) {
        struct PostCopyFD *pcfd = &g_array_index(mis->postcopy_remote_fds,
                                                 struct PostCopyFD, index);
        pfd[2 + index].fd = pcfd->fd;
        pfd[2 + index].events = POLLIN;
        trace_postcopy_ram_fault_thread_fds_extra(2 + index, pcfd->idstr,
                                                  pcfd->fd);
    }

    while (true) {
        ram_addr_t rb_offset;
        int poll_result;

        poll_result = poll(pfd, pfd_len, -1 );
        if (poll_result == -1) {
            error_report("%s: userfault poll: %s", __func__, strerror(errno));
            break;
        }

        if (!mis->to_src_file) {
            postcopy_pause_fault_thread(mis);
        }

        if (pfd[1].revents) {
            uint64_t tmp64 = 0;

            if (read(mis->userfault_event_fd, &tmp64, 8) != 8) {
                error_report("%s: read() failed", __func__);
            }

            if (qatomic_read(&mis->fault_thread_quit)) {
                trace_postcopy_ram_fault_thread_quit();
                break;
            }
        }

        if (pfd[0].revents) {
            poll_result--;
            ret = read(mis->userfault_fd, &msg, sizeof(msg));
            if (ret != sizeof(msg)) {
                if (errno == EAGAIN) {
                    continue;
                }
                if (ret < 0) {
                    error_report("%s: Failed to read full userfault "
                                 "message: %s",
                                 __func__, strerror(errno));
                    break;
                } else {
                    error_report("%s: Read %d bytes from userfaultfd "
                                 "expected %zd",
                                 __func__, ret, sizeof(msg));
                    break;
                }
            }
            if (msg.event != UFFD_EVENT_PAGEFAULT) {
                error_report("%s: Read unexpected event %ud from userfaultfd",
                             __func__, msg.event);
                continue;
            }

            rb = qemu_ram_block_from_host(
                     (void *)(uintptr_t)msg.arg.pagefault.address,
                     true, &rb_offset);
            if (!rb) {
                error_report("postcopy_ram_fault_thread: Fault outside guest: %"
                             PRIx64, (uint64_t)msg.arg.pagefault.address);
                break;
            }

            rb_offset = ROUND_DOWN(rb_offset, qemu_ram_pagesize(rb));
            trace_postcopy_ram_fault_thread_request(msg.arg.pagefault.address,
                                                qemu_ram_get_idstr(rb),
                                                rb_offset,
                                                msg.arg.pagefault.feat.ptid);
retry:
            ret = postcopy_request_page(mis, rb, rb_offset,
                                        msg.arg.pagefault.address,
                                        msg.arg.pagefault.feat.ptid);
            if (ret) {
                postcopy_pause_fault_thread(mis);
                goto retry;
            }
        }

        for (index = 2; index < pfd_len && poll_result; index++) {
            if (pfd[index].revents) {
                struct PostCopyFD *pcfd =
                    &g_array_index(mis->postcopy_remote_fds,
                                   struct PostCopyFD, index - 2);

                poll_result--;
                if (pfd[index].revents & POLLERR) {
                    error_report("%s: POLLERR on poll %zd fd=%d",
                                 __func__, index, pcfd->fd);
                    pfd[index].events = 0;
                    continue;
                }

                ret = read(pcfd->fd, &msg, sizeof(msg));
                if (ret != sizeof(msg)) {
                    if (errno == EAGAIN) {
                        continue;
                    }
                    if (ret < 0) {
                        error_report("%s: Failed to read full userfault "
                                     "message: %s (shared) revents=%d",
                                     __func__, strerror(errno),
                                     pfd[index].revents);
                        break;
                    } else {
                        error_report("%s: Read %d bytes from userfaultfd "
                                     "expected %zd (shared)",
                                     __func__, ret, sizeof(msg));
                        break;
                    }
                }
                if (msg.event != UFFD_EVENT_PAGEFAULT) {
                    error_report("%s: Read unexpected event %ud "
                                 "from userfaultfd (shared)",
                                 __func__, msg.event);
                    continue;
                }
                ret = pcfd->handler(pcfd, &msg);
                if (ret) {
                    error_report("%s: Failed to resolve shared fault on %zd/%s",
                                 __func__, index, pcfd->idstr);
                }
            }
        }
    }
    rcu_unregister_thread();
    trace_postcopy_ram_fault_thread_exit();
    g_free(pfd);
    return NULL;
}

static int postcopy_temp_pages_setup(MigrationIncomingState *mis)
{
    PostcopyTmpPage *tmp_page;
    int err;
    unsigned i, channels;
    void *temp_page;

    if (migrate_postcopy_preempt()) {
        mis->postcopy_channels = RAM_CHANNEL_MAX;
    } else {
        mis->postcopy_channels = 1;
    }

    channels = mis->postcopy_channels;
    mis->postcopy_tmp_pages = g_new0(PostcopyTmpPage, channels);

    for (i = 0; i < channels; i++) {
        tmp_page = &mis->postcopy_tmp_pages[i];
        temp_page = mmap(NULL, mis->largest_page_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (temp_page == MAP_FAILED) {
            err = errno;
            error_report("%s: Failed to map postcopy_tmp_pages[%d]: %s",
                         __func__, i, strerror(err));
            return -err;
        }
        tmp_page->tmp_huge_page = temp_page;
        postcopy_temp_page_reset(tmp_page);
    }

    mis->postcopy_tmp_zero_page = mmap(NULL, mis->largest_page_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mis->postcopy_tmp_zero_page == MAP_FAILED) {
        err = errno;
        mis->postcopy_tmp_zero_page = NULL;
        error_report("%s: Failed to map large zero page %s",
                     __func__, strerror(err));
        return -err;
    }

    memset(mis->postcopy_tmp_zero_page, '\0', mis->largest_page_size);

    return 0;
}

int postcopy_ram_incoming_setup(MigrationIncomingState *mis)
{
    Error *local_err = NULL;

    mis->userfault_fd = uffd_open(O_CLOEXEC | O_NONBLOCK);
    if (mis->userfault_fd == -1) {
        error_report("%s: Failed to open userfault fd: %s", __func__,
                     strerror(errno));
        return -1;
    }

    if (!ufd_check_and_apply(mis->userfault_fd, mis, &local_err)) {
        error_report_err(local_err);
        return -1;
    }

    if (migrate_postcopy_blocktime()) {
        assert(mis->blocktime_ctx == NULL);
        mis->blocktime_ctx = blocktime_context_new();
    }

    mis->userfault_event_fd = eventfd(0, EFD_CLOEXEC);
    if (mis->userfault_event_fd == -1) {
        error_report("%s: Opening userfault_event_fd: %s", __func__,
                     strerror(errno));
        close(mis->userfault_fd);
        return -1;
    }

    postcopy_thread_create(mis, &mis->fault_thread,
                           MIGRATION_THREAD_DST_FAULT,
                           postcopy_ram_fault_thread, QEMU_THREAD_JOINABLE);
    mis->have_fault_thread = true;

    if (foreach_not_ignored_block(ram_block_enable_notify, mis)) {
        error_report("ram_block_enable_notify failed");
        return -1;
    }

    if (postcopy_temp_pages_setup(mis)) {
        return -1;
    }

    if (migrate_postcopy_preempt()) {
        postcopy_thread_create(mis, &mis->postcopy_prio_thread,
                               MIGRATION_THREAD_DST_PREEMPT,
                               postcopy_preempt_thread, QEMU_THREAD_JOINABLE);
        mis->preempt_thread_status = PREEMPT_THREAD_CREATED;
    }

    trace_postcopy_ram_enable_notify();

    return 0;
}

static int qemu_ufd_copy_ioctl(MigrationIncomingState *mis, void *host_addr,
                               void *from_addr, uint64_t pagesize, RAMBlock *rb)
{
    int userfault_fd = mis->userfault_fd;
    int ret;

    if (from_addr) {
        ret = uffd_copy_page(userfault_fd, host_addr, from_addr, pagesize,
                             false);
    } else {
        ret = uffd_zero_page(userfault_fd, host_addr, pagesize, false);
    }
    if (!ret) {
        qemu_mutex_lock(&mis->page_request_mutex);
        ramblock_recv_bitmap_set_range(rb, host_addr,
                                       pagesize / qemu_target_page_size());
        if (g_tree_lookup(mis->page_requested, host_addr)) {
            g_tree_remove(mis->page_requested, host_addr);
            int left_pages = qatomic_dec_fetch(&mis->page_requested_count);

            trace_postcopy_page_req_del(host_addr, mis->page_requested_count);
            smp_mb();
            if (mis->preempt_thread_status == PREEMPT_THREAD_QUIT &&
                left_pages == 0) {
                qemu_cond_signal(&mis->page_request_cond);
            }
        }
        mark_postcopy_blocktime_end((uintptr_t)host_addr);
        qemu_mutex_unlock(&mis->page_request_mutex);
    }
    return ret;
}

int postcopy_notify_shared_wake(RAMBlock *rb, uint64_t offset)
{
    int i;
    MigrationIncomingState *mis = migration_incoming_get_current();
    GArray *pcrfds = mis->postcopy_remote_fds;

    for (i = 0; i < pcrfds->len; i++) {
        struct PostCopyFD *cur = &g_array_index(pcrfds, struct PostCopyFD, i);
        int ret = cur->waker(cur, rb, offset);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    int e;

    e = qemu_ufd_copy_ioctl(mis, host, from, pagesize, rb);
    if (e) {
        return e;
    }

    trace_postcopy_place_page(host);
    return postcopy_notify_shared_wake(rb,
                                       qemu_ram_block_host_offset(rb, host));
}

int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                             RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    trace_postcopy_place_page_zero(host);

    if (qemu_ram_is_uf_zeroable(rb)) {
        int e;
        e = qemu_ufd_copy_ioctl(mis, host, NULL, pagesize, rb);
        if (e) {
            return e;
        }
        return postcopy_notify_shared_wake(rb,
                                           qemu_ram_block_host_offset(rb,
                                                                      host));
    } else {
        return postcopy_place_page(mis, host, mis->postcopy_tmp_zero_page, rb);
    }
}

#else
void fill_destination_postcopy_migration_info(MigrationInfo *info)
{
}

bool postcopy_ram_supported_by_host(MigrationIncomingState *mis, Error **errp)
{
    error_report("%s: No OS support", __func__);
    return false;
}

int postcopy_ram_incoming_init(MigrationIncomingState *mis, Error **errp)
{
    error_report("postcopy_ram_incoming_init: No OS support");
    return -1;
}

int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    g_assert_not_reached();
}

int postcopy_ram_incoming_setup(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    g_assert_not_reached();
}

int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                        RAMBlock *rb)
{
    g_assert_not_reached();
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    g_assert_not_reached();
}

void mark_postcopy_blocktime_begin(uintptr_t addr, uint32_t ptid,
                                   RAMBlock *rb)
{
}
#endif

void postcopy_temp_page_reset(PostcopyTmpPage *tmp_page)
{
    tmp_page->target_pages = 0;
    tmp_page->host_addr = NULL;
    tmp_page->all_zero = true;
}

void postcopy_fault_thread_notify(MigrationIncomingState *mis)
{
    uint64_t tmp64 = 1;

    if (write(mis->userfault_event_fd, &tmp64, 8) != 8) {
        error_report("%s: incrementing failed: %s", __func__,
                     strerror(errno));
    }
}

static PostcopyDiscardState pds = {0};
void postcopy_discard_send_init(MigrationState *ms, const char *name)
{
    pds.ramblock_name = name;
    pds.cur_entry = 0;
    pds.nsentwords = 0;
    pds.nsentcmds = 0;
}

void postcopy_discard_send_range(MigrationState *ms, unsigned long start,
                                 unsigned long length)
{
    size_t tp_size = qemu_target_page_size();
    pds.start_list[pds.cur_entry] = start  * tp_size;
    pds.length_list[pds.cur_entry] = length * tp_size;
    trace_postcopy_discard_send_range(pds.ramblock_name, start, length);
    pds.cur_entry++;
    pds.nsentwords++;

    if (pds.cur_entry == MAX_DISCARDS_PER_COMMAND) {
        qemu_savevm_send_postcopy_ram_discard(ms->to_dst_file,
                                              pds.ramblock_name,
                                              pds.cur_entry,
                                              pds.start_list,
                                              pds.length_list);
        pds.nsentcmds++;
        pds.cur_entry = 0;
    }
}

void postcopy_discard_send_finish(MigrationState *ms)
{
    if (pds.cur_entry) {
        qemu_savevm_send_postcopy_ram_discard(ms->to_dst_file,
                                              pds.ramblock_name,
                                              pds.cur_entry,
                                              pds.start_list,
                                              pds.length_list);
        pds.nsentcmds++;
    }

    trace_postcopy_discard_send_finish(pds.ramblock_name, pds.nsentwords,
                                       pds.nsentcmds);
}

static PostcopyState incoming_postcopy_state;

PostcopyState  postcopy_state_get(void)
{
    return qatomic_load_acquire(&incoming_postcopy_state);
}

PostcopyState postcopy_state_set(PostcopyState new_state)
{
    return qatomic_xchg(&incoming_postcopy_state, new_state);
}

void postcopy_register_shared_ufd(struct PostCopyFD *pcfd)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    mis->postcopy_remote_fds = g_array_append_val(mis->postcopy_remote_fds,
                                                  *pcfd);
}

void postcopy_unregister_shared_ufd(struct PostCopyFD *pcfd)
{
    guint i;
    MigrationIncomingState *mis = migration_incoming_get_current();
    GArray *pcrfds = mis->postcopy_remote_fds;

    if (!pcrfds) {
        return;
    }
    for (i = 0; i < pcrfds->len; i++) {
        struct PostCopyFD *cur = &g_array_index(pcrfds, struct PostCopyFD, i);
        if (cur->fd == pcfd->fd) {
            mis->postcopy_remote_fds = g_array_remove_index(pcrfds, i);
            return;
        }
    }
}

void postcopy_preempt_new_channel(MigrationIncomingState *mis, QEMUFile *file)
{
    qemu_file_set_blocking(file, true, &error_abort);
    mis->postcopy_qemufile_dst = file;
    qemu_sem_post(&mis->postcopy_qemufile_dst_done);
    trace_postcopy_preempt_new_channel();
}

static void
postcopy_preempt_send_channel_done(MigrationState *s,
                                   QIOChannel *ioc, Error *local_err)
{
    if (local_err) {
        migrate_set_error(s, local_err);
        error_free(local_err);
    } else {
        migration_ioc_register_yank(ioc);
        s->postcopy_qemufile_src = qemu_file_new_output(ioc);
        trace_postcopy_preempt_new_channel();
    }

    qemu_sem_post(&s->postcopy_qemufile_src_sem);
}

static void
postcopy_preempt_tls_handshake(QIOTask *task, gpointer opaque)
{
    g_autoptr(QIOChannel) ioc = QIO_CHANNEL(qio_task_get_source(task));
    MigrationState *s = opaque;
    Error *local_err = NULL;

    qio_task_propagate_error(task, &local_err);
    postcopy_preempt_send_channel_done(s, ioc, local_err);
}

static void
postcopy_preempt_send_channel_new(QIOTask *task, gpointer opaque)
{
    g_autoptr(QIOChannel) ioc = QIO_CHANNEL(qio_task_get_source(task));
    MigrationState *s = opaque;
    QIOChannelTLS *tioc;
    Error *local_err = NULL;

    if (qio_task_propagate_error(task, &local_err)) {
        goto out;
    }

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        tioc = migration_tls_client_create(ioc, s->hostname, &local_err);
        if (!tioc) {
            goto out;
        }
        trace_postcopy_preempt_tls_handshake();
        qio_channel_set_name(QIO_CHANNEL(tioc), "migration-tls-preempt");
        qio_channel_tls_handshake(tioc, postcopy_preempt_tls_handshake,
                                  s, NULL, NULL);
        return;
    }

out:
    postcopy_preempt_send_channel_done(s, ioc, local_err);
}

int postcopy_preempt_establish_channel(MigrationState *s)
{
    if (!migrate_postcopy_preempt()) {
        return 0;
    }

    if (!s->preempt_pre_7_2) {
        postcopy_preempt_setup(s);
    }

    qemu_sem_wait(&s->postcopy_qemufile_src_sem);

    return s->postcopy_qemufile_src ? 0 : -1;
}

void postcopy_preempt_setup(MigrationState *s)
{
    socket_send_channel_create(postcopy_preempt_send_channel_new, s);
}

static void postcopy_pause_ram_fast_load(MigrationIncomingState *mis)
{
    trace_postcopy_pause_fast_load();
    qemu_mutex_unlock(&mis->postcopy_prio_thread_mutex);
    qemu_sem_wait(&mis->postcopy_pause_sem_fast_load);
    qemu_mutex_lock(&mis->postcopy_prio_thread_mutex);
    trace_postcopy_pause_fast_load_continued();
}

static bool preempt_thread_should_run(MigrationIncomingState *mis)
{
    return mis->preempt_thread_status != PREEMPT_THREAD_QUIT;
}

void *postcopy_preempt_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    int ret;

    trace_postcopy_preempt_thread_entry();

    rcu_register_thread();

    qemu_event_set(&mis->thread_sync_event);

    qemu_sem_wait(&mis->postcopy_qemufile_dst_done);

    qemu_mutex_lock(&mis->postcopy_prio_thread_mutex);
    while (preempt_thread_should_run(mis)) {
        ret = ram_load_postcopy(mis->postcopy_qemufile_dst,
                                RAM_CHANNEL_POSTCOPY);
        if (ret && preempt_thread_should_run(mis)) {
            postcopy_pause_ram_fast_load(mis);
        } else {
            break;
        }
    }
    qemu_mutex_unlock(&mis->postcopy_prio_thread_mutex);

    rcu_unregister_thread();

    trace_postcopy_preempt_thread_exit();

    return NULL;
}

bool postcopy_is_paused(MigrationStatus status)
{
    return status == MIGRATION_STATUS_POSTCOPY_PAUSED ||
        status == MIGRATION_STATUS_POSTCOPY_RECOVER_SETUP;
}

static void postcopy_listen_thread_bh(void *opaque)
{
    MigrationState *s = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();

    migration_incoming_state_destroy();

    if (mis->state == MIGRATION_STATUS_FAILED && mis->exit_on_error) {
        WITH_QEMU_LOCK_GUARD(&s->error_mutex) {
            error_report_err(s->error);
            s->error = NULL;
        }
        exit(1);
    }
}

static void *postcopy_listen_thread(void *opaque)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    QEMUFile *f = mis->from_src_file;
    int load_res;
    MigrationState *migr = migrate_get_current();
    Error *local_err = NULL;

    object_ref(OBJECT(migr));

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      mis->to_src_file ? MIGRATION_STATUS_POSTCOPY_DEVICE :
                                         MIGRATION_STATUS_POSTCOPY_ACTIVE);
    qemu_event_set(&mis->thread_sync_event);
    trace_postcopy_ram_listen_thread_start();

    rcu_register_thread();
    qemu_file_set_blocking(f, true, &error_fatal);

    load_res = qemu_loadvm_state_main(f, mis, &local_err);

    f = mis->from_src_file;

    qemu_file_set_blocking(f, false, &error_fatal);

    trace_postcopy_ram_listen_thread_exit();
    if (load_res < 0) {
        qemu_file_set_error(f, load_res);
        dirty_bitmap_mig_cancel_incoming();
        error_prepend(&local_err,
                      "loadvm failed during postcopy: %d: ", load_res);
        if (postcopy_state_get() == POSTCOPY_INCOMING_RUNNING &&
            !migrate_postcopy_ram() && migrate_dirty_bitmaps())
        {
            error_append_hint(&local_err,
                              "All state is migrated except dirty bitmaps."
                              " Some dirty bitmaps may be lost, but any"
                              " migrated dirty bitmaps are valid.");
            error_report_err(local_err);
        } else {
            migrate_set_error(migr, local_err);
            error_report_err(local_err);
            migrate_set_state(&mis->state, mis->state, MIGRATION_STATUS_FAILED);
            goto out;
        }
    }
    qemu_event_wait(&mis->main_thread_load_event);

    migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                                   MIGRATION_STATUS_COMPLETED);

out:
    rcu_unregister_thread();
    postcopy_state_set(POSTCOPY_INCOMING_END);

    migration_bh_schedule(postcopy_listen_thread_bh, NULL);

    object_unref(OBJECT(migr));

    return NULL;
}

int postcopy_incoming_setup(MigrationIncomingState *mis, Error **errp)
{
    if (migrate_postcopy_ram()) {
        if (postcopy_ram_incoming_setup(mis)) {
            postcopy_ram_incoming_cleanup(mis);
            error_setg(errp, "Failed to setup incoming postcopy RAM blocks");
            return -1;
        }
    }

    trace_loadvm_postcopy_handle_listen("after uffd");

    if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_LISTEN, errp)) {
        return -1;
    }

    mis->have_listen_thread = true;
    postcopy_thread_create(mis, &mis->listen_thread,
                           MIGRATION_THREAD_DST_LISTEN,
                           postcopy_listen_thread, QEMU_THREAD_JOINABLE);

    return 0;
}

int postcopy_incoming_cleanup(MigrationIncomingState *mis)
{
    int rc = 0;

    if (mis->have_listen_thread) {
        qemu_thread_join(&mis->listen_thread);
        mis->have_listen_thread = false;
    }

    if (migrate_postcopy_ram()) {
        rc = postcopy_ram_incoming_cleanup(mis);
    }

    return rc;
}
