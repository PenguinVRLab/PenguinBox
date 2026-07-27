/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_H
#define QEMU_MIGRATION_H

#include "exec/cpu-common.h"
#include "hw/qdev-core.h"
#include "qapi/qapi-types-migration.h"
#include "qobject/json-writer.h"
#include "qemu/thread.h"
#include "qemu/coroutine.h"
#include "io/channel.h"
#include "io/channel-buffer.h"
#include "net/announce.h"
#include "qom/object.h"
#include "postcopy-ram.h"
#include "system/runstate.h"
#include "migration/misc.h"

#define  MIGRATION_THREAD_SNAPSHOT          "mig/snapshot"
#define  MIGRATION_THREAD_DIRTY_RATE        "mig/dirtyrate"

#define  MIGRATION_THREAD_SRC_MAIN          "mig/src/main"
#define  MIGRATION_THREAD_SRC_MULTIFD       "mig/src/send_%d"
#define  MIGRATION_THREAD_SRC_RETURN        "mig/src/return"
#define  MIGRATION_THREAD_SRC_TLS           "mig/src/tls"

#define  MIGRATION_THREAD_DST_COLO          "mig/dst/colo"
#define  MIGRATION_THREAD_DST_MULTIFD       "mig/dst/recv_%d"
#define  MIGRATION_THREAD_DST_FAULT         "mig/dst/fault"
#define  MIGRATION_THREAD_DST_LISTEN        "mig/dst/listen"
#define  MIGRATION_THREAD_DST_PREEMPT       "mig/dst/preempt"

struct PostcopyBlocktimeContext;
typedef struct ThreadPool ThreadPool;

#define  MIGRATION_RESUME_ACK_VALUE  (1)

#define CLEAR_BITMAP_SHIFT_MIN             6
#define CLEAR_BITMAP_SHIFT_DEFAULT        18
#define CLEAR_BITMAP_SHIFT_MAX            31

typedef struct {
    void *tmp_huge_page;
    void *host_addr;
    unsigned int target_pages;
    bool all_zero;
} PostcopyTmpPage;

typedef enum {
    PREEMPT_THREAD_NONE = 0,
    PREEMPT_THREAD_CREATED,
    PREEMPT_THREAD_QUIT,
} PreemptThreadStatus;

struct MigrationIncomingState {
    QEMUFile *from_src_file;
    RAMBlock *last_recv_block[RAM_CHANNEL_MAX];
    void *transport_data;
    void (*transport_cleanup)(void *data);
    QemuEvent  thread_sync_event;
    QemuEvent main_thread_load_event;

    AnnounceTimer  announce_timer;

    size_t         largest_page_size;
    bool           have_fault_thread;
    QemuThread     fault_thread;
    bool           fault_thread_quit;

    bool           have_listen_thread;
    QemuThread     listen_thread;

    int       userfault_fd;
    int       userfault_event_fd;
    QEMUFile *to_src_file;
    QemuMutex rp_mutex;
    RAMBlock *last_rb;
    unsigned int postcopy_channels;
    QEMUFile *postcopy_qemufile_dst;
    QemuSemaphore postcopy_qemufile_dst_done;
    QemuThread postcopy_prio_thread;
    volatile PreemptThreadStatus preempt_thread_status;
    QemuMutex postcopy_prio_thread_mutex;
    PostcopyTmpPage *postcopy_tmp_pages;
    void     *postcopy_tmp_zero_page;
    GArray   *postcopy_remote_fds;

    MigrationStatus state;

    Coroutine *loadvm_co;

    Coroutine *colo_incoming_co;
    QemuEvent colo_incoming_event;

    ThreadPool *load_threads;
    bool load_threads_abort;

    struct PostcopyBlocktimeContext *blocktime_ctx;

    QemuSemaphore postcopy_pause_sem_dst;
    QemuSemaphore postcopy_pause_sem_fault;
    QemuSemaphore postcopy_pause_sem_fast_load;

    SocketAddressList *socket_address_list;

    GTree *page_requested;
    int page_requested_count;
    QemuMutex page_request_mutex;
    QemuCond page_request_cond;

    unsigned int switchover_ack_pending_num;

    bool exit_on_error;
};

MigrationIncomingState *migration_incoming_get_current(void);
void migration_incoming_state_destroy(void);
void migration_incoming_transport_cleanup(MigrationIncomingState *mis);
void migration_incoming_qemu_exit(void);
void fill_destination_postcopy_migration_info(MigrationInfo *info);

#define TYPE_MIGRATION "migration"

typedef struct MigrationClass MigrationClass;
DECLARE_OBJ_CHECKERS(MigrationState, MigrationClass,
                     MIGRATION_OBJ, TYPE_MIGRATION)

struct MigrationClass {
    DeviceClass parent_class;
};

struct MigrationState {
    DeviceState parent_obj;

    QemuThread thread;
    QEMUFile *to_dst_file;
    QEMUFile *postcopy_qemufile_src;
    QemuSemaphore postcopy_qemufile_src_sem;
    QIOChannelBuffer *bioc;
    QemuMutex qemu_file_lock;

    QemuSemaphore rate_limit_sem;

    uint64_t iteration_initial_pages;

    double pages_per_second;

    uint64_t iteration_initial_bytes;
    int64_t iteration_start_time;
    uint64_t threshold_size;

    MigrationParameters parameters;

    MigrationStatus state;

    struct {
        QEMUFile     *from_dst_file;
        QemuThread    rp_thread;
        bool          rp_thread_created;
        QemuSemaphore rp_sem;
        QemuSemaphore rp_pong_acks;
    } rp_state;

    double mbps;
    int64_t start_time;
    int64_t total_time;
    int64_t downtime_start;
    int64_t downtime;
    int64_t expected_downtime;
    bool capabilities[MIGRATION_CAPABILITY__MAX];
    int64_t setup_time;

    RunState vm_old_state;

    bool start_postcopy;

    bool migration_thread_running;

    QemuSemaphore wait_unplug_sem;

    QemuEvent pause_event;

    QemuEvent colo_exit_event;

    QemuEvent colo_checkpoint_event;
    int64_t colo_checkpoint_time;
    QEMUTimer *colo_delay_timer;

    Error *error;
    QemuMutex error_mutex;

    bool store_global_state;

    bool send_configuration;
    bool send_section_footer;
    bool send_switchover_start;

    QemuSemaphore postcopy_pause_sem;
    bool preempt_pre_7_2;

    bool multifd_flush_after_each_section;

    bool multifd_clean_tls_termination;

    uint8_t clear_bitmap_shift;

    char *hostname;

    JSONWriter *vmdesc;

    bool switchover_acked;
    bool rdma_migration;

    bool postcopy_package_loaded;
    QemuEvent postcopy_package_loaded_event;

    GSource *hup_source;
};

void migrate_set_state(MigrationStatus *state, MigrationStatus old_state,
                       MigrationStatus new_state);

void migration_fd_process_incoming(QEMUFile *f);
void migration_ioc_process_incoming(QIOChannel *ioc, Error **errp);
void migration_incoming_process(void);

bool  migration_has_all_channels(void);

void migrate_set_error(MigrationState *s, const Error *error);
bool migrate_has_error(MigrationState *s);

void migration_connect(MigrationState *s, Error *error_in);

int migration_call_notifiers(MigrationState *s, MigrationEventType type,
                             Error **errp);

int migrate_init(MigrationState *s, Error **errp);
bool migration_is_blocked(Error **errp);
bool migration_in_postcopy(void);
bool migration_postcopy_is_alive(MigrationStatus state);
MigrationState *migrate_get_current(void);
bool migration_has_failed(MigrationState *);
bool migrate_mode_is_cpr(MigrationState *);

uint64_t ram_get_total_transferred_pages(void);

void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value);
int migrate_send_rp_req_pages(MigrationIncomingState *mis, RAMBlock *rb,
                              ram_addr_t start, uint64_t haddr, uint32_t tid);
int migrate_send_rp_message_req_pages(MigrationIncomingState *mis,
                                      RAMBlock *rb, ram_addr_t start);
void migrate_send_rp_recv_bitmap(MigrationIncomingState *mis,
                                 char *block_name);
void migrate_send_rp_resume_ack(MigrationIncomingState *mis, uint32_t value);
int migrate_send_rp_switchover_ack(MigrationIncomingState *mis);

void dirty_bitmap_mig_before_vm_start(void);
void dirty_bitmap_mig_cancel_outgoing(void);
void dirty_bitmap_mig_cancel_incoming(void);
bool check_dirty_bitmap_mig_alias_map(const BitmapMigrationNodeAliasList *bbm,
                                      Error **errp);

void migrate_add_address(SocketAddress *address);
int foreach_not_ignored_block(RAMBlockIterFunc func, void *opaque);

#define qemu_ram_foreach_block \
  #warning "Use foreach_not_ignored_block in migration code"

void migration_make_urgent_request(void);
void migration_consume_urgent_request(void);
bool migration_rate_limit(void);
void migration_bh_schedule(QEMUBHFunc *cb, void *opaque);
void migration_cancel(void);

void migration_populate_vfio_info(MigrationInfo *info);
void migration_reset_vfio_bytes_transferred(void);
void postcopy_temp_page_reset(PostcopyTmpPage *tmp_page);

int migration_rp_wait(MigrationState *s);
void migration_rp_kick(MigrationState *s);

void migration_bitmap_sync_precopy(bool last_stage);

void dirty_bitmap_mig_init(void);
bool should_send_vmdesc(void);

#endif
