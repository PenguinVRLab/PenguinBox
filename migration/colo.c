/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/system.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "migration.h"
#include "qemu-file.h"
#include "savevm.h"
#include "migration/colo.h"
#include "io/channel-buffer.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "migration/failover.h"
#include "migration/ram.h"
#include "block/replication.h"
#include "net/colo-compare.h"
#include "net/colo.h"
#include "block/block.h"
#include "qapi/qapi-events-migration.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "net/filter.h"
#include "options.h"

static bool vmstate_loading;
static Notifier packets_compare_notifier;

static COLOMode last_colo_mode;

#define COLO_BUFFER_BASE_SIZE (4 * 1024 * 1024)

bool migration_in_colo_state(void)
{
    MigrationState *s = migrate_get_current();

    return (s->state == MIGRATION_STATUS_COLO);
}

bool migration_incoming_in_colo_state(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    return mis && (mis->state == MIGRATION_STATUS_COLO);
}

static bool colo_runstate_is_stopped(void)
{
    return runstate_check(RUN_STATE_COLO) || !runstate_is_running();
}

static void colo_checkpoint_notify(void)
{
    MigrationState *s = migrate_get_current();
    int64_t next_notify_time;

    qemu_event_set(&s->colo_checkpoint_event);
    s->colo_checkpoint_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    next_notify_time = s->colo_checkpoint_time + migrate_checkpoint_delay();
    timer_mod(s->colo_delay_timer, next_notify_time);
}

static void colo_checkpoint_notify_timer(void *opaque)
{
    colo_checkpoint_notify();
}

void colo_checkpoint_delay_set(void)
{
    if (migration_in_colo_state()) {
        colo_checkpoint_notify();
    }
}

static void secondary_vm_do_failover(void)
{
    int old_state;
    MigrationIncomingState *mis = migration_incoming_get_current();
    Error *local_err = NULL;

    if (vmstate_loading) {
        old_state = failover_set_state(FAILOVER_STATUS_ACTIVE,
                        FAILOVER_STATUS_RELAUNCH);
        if (old_state != FAILOVER_STATUS_ACTIVE) {
            error_report("Unknown error while do failover for secondary VM,"
                         "old_state: %s", FailoverStatus_str(old_state));
        }
        return;
    }

    migrate_set_state(&mis->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    replication_stop_all(true, &local_err);
    if (local_err) {
        error_report_err(local_err);
        local_err = NULL;
    }

    colo_notify_filters_event(COLO_EVENT_FAILOVER, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    if (!autostart) {
        error_report("\"-S\" qemu option will be ignored in secondary side");
        autostart = true;
    }
    if (mis->from_src_file) {
        qemu_file_shutdown(mis->from_src_file);
    }
    if (mis->to_src_file) {
        qemu_file_shutdown(mis->to_src_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_ACTIVE,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_ACTIVE) {
        error_report("Incorrect state (%s) while doing failover for "
                     "secondary VM", FailoverStatus_str(old_state));
        return;
    }
    qemu_event_set(&mis->colo_incoming_event);

    if (mis->colo_incoming_co) {
        qemu_coroutine_enter(mis->colo_incoming_co);
    }
}

static void primary_vm_do_failover(void)
{
    MigrationState *s = migrate_get_current();
    int old_state;
    Error *local_err = NULL;

    migrate_set_state(&s->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);
    colo_checkpoint_notify();

    if (s->to_dst_file) {
        qemu_file_shutdown(s->to_dst_file);
    }
    if (s->rp_state.from_dst_file) {
        qemu_file_shutdown(s->rp_state.from_dst_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_ACTIVE,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_ACTIVE) {
        error_report("Incorrect state (%s) while doing failover for Primary VM",
                     FailoverStatus_str(old_state));
        return;
    }

    replication_stop_all(true, &local_err);
    if (local_err) {
        error_report_err(local_err);
        local_err = NULL;
    }

    qemu_event_set(&s->colo_exit_event);
}

COLOMode get_colo_mode(void)
{
    if (migration_in_colo_state()) {
        return COLO_MODE_PRIMARY;
    } else if (migration_incoming_in_colo_state()) {
        return COLO_MODE_SECONDARY;
    } else {
        return COLO_MODE_NONE;
    }
}

void colo_do_failover(void)
{
    if (!colo_runstate_is_stopped()) {
        vm_stop_force_state(RUN_STATE_COLO);
    }

    switch (last_colo_mode = get_colo_mode()) {
    case COLO_MODE_PRIMARY:
        primary_vm_do_failover();
        break;
    case COLO_MODE_SECONDARY:
        secondary_vm_do_failover();
        break;
    default:
        error_report("colo_do_failover failed because the colo mode"
                     " could not be obtained");
    }
}

void qmp_xen_set_replication(bool enable, bool primary,
                             bool has_failover, bool failover,
                             Error **errp)
{
    ReplicationMode mode = primary ?
                           REPLICATION_MODE_PRIMARY :
                           REPLICATION_MODE_SECONDARY;

    if (has_failover && enable) {
        error_setg(errp, "Parameter 'failover' is only for"
                   " stopping replication");
        return;
    }

    if (enable) {
        replication_start_all(mode, errp);
    } else {
        if (!has_failover) {
            failover = NULL;
        }
        replication_stop_all(failover, failover ? NULL : errp);
    }
}

ReplicationStatus *qmp_query_xen_replication_status(Error **errp)
{
    Error *err = NULL;
    ReplicationStatus *s = g_new0(ReplicationStatus, 1);

    replication_get_error_all(&err);
    if (err) {
        s->error = true;
        s->desc = g_strdup(error_get_pretty(err));
    } else {
        s->error = false;
    }

    error_free(err);
    return s;
}

void qmp_xen_colo_do_checkpoint(Error **errp)
{
    Error *err = NULL;

    replication_do_checkpoint_all(&err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    colo_notify_filters_event(COLO_EVENT_CHECKPOINT, errp);
}

COLOStatus *qmp_query_colo_status(Error **errp)
{
    COLOStatus *s = g_new0(COLOStatus, 1);

    s->mode = get_colo_mode();
    s->last_mode = last_colo_mode;

    switch (failover_get_state()) {
    case FAILOVER_STATUS_NONE:
        s->reason = COLO_EXIT_REASON_NONE;
        break;
    case FAILOVER_STATUS_COMPLETED:
        s->reason = COLO_EXIT_REASON_REQUEST;
        break;
    default:
        if (migration_in_colo_state()) {
            s->reason = COLO_EXIT_REASON_PROCESSING;
        } else {
            s->reason = COLO_EXIT_REASON_ERROR;
        }
    }

    return s;
}

static void colo_send_message(QEMUFile *f, COLOMessage msg,
                              Error **errp)
{
    int ret;

    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return;
    }
    qemu_put_be32(f, msg);
    ret = qemu_fflush(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't send COLO message");
    }
    trace_colo_send_message(COLOMessage_str(msg));
}

static void colo_send_message_value(QEMUFile *f, COLOMessage msg,
                                    uint64_t value, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    colo_send_message(f, msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_put_be64(f, value);
    ret = qemu_fflush(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to send value for message:%s",
                         COLOMessage_str(msg));
    }
}

static COLOMessage colo_receive_message(QEMUFile *f, Error **errp)
{
    COLOMessage msg;
    int ret;

    msg = qemu_get_be32(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't receive COLO message");
        return msg;
    }
    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return msg;
    }
    trace_colo_receive_message(COLOMessage_str(msg));
    return msg;
}

static void colo_receive_check_message(QEMUFile *f, COLOMessage expect_msg,
                                       Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (msg != expect_msg) {
        error_setg(errp, "Unexpected COLO message %d, expected %d",
                          msg, expect_msg);
    }
}

static uint64_t colo_receive_message_value(QEMUFile *f, uint32_t expect_msg,
                                           Error **errp)
{
    Error *local_err = NULL;
    uint64_t value;
    int ret;

    colo_receive_check_message(f, expect_msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return 0;
    }

    value = qemu_get_be64(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to get value for COLO message: %s",
                         COLOMessage_str(expect_msg));
    }
    return value;
}

static int colo_do_checkpoint_transaction(MigrationState *s,
                                          QIOChannelBuffer *bioc,
                                          QEMUFile *fb)
{
    Error *local_err = NULL;
    int ret = -1;

    colo_send_message(s->to_dst_file, COLO_MESSAGE_CHECKPOINT_REQUEST,
                      &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                    COLO_MESSAGE_CHECKPOINT_REPLY, &local_err);
    if (local_err) {
        goto out;
    }
    qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);
    bioc->usage = 0;

    bql_lock();
    if (failover_get_state() != FAILOVER_STATUS_NONE) {
        bql_unlock();
        goto out;
    }
    vm_stop_force_state(RUN_STATE_COLO);
    bql_unlock();
    trace_colo_vm_state_change("run", "stop");
    if (failover_get_state() != FAILOVER_STATUS_NONE) {
        goto out;
    }
    bql_lock();

    replication_do_checkpoint_all(&local_err);
    if (local_err) {
        bql_unlock();
        goto out;
    }

    colo_send_message(s->to_dst_file, COLO_MESSAGE_VMSTATE_SEND, &local_err);
    if (local_err) {
        bql_unlock();
        goto out;
    }

    qemu_savevm_maybe_send_switchover_start(s->to_dst_file);

    ret = qemu_save_device_state(fb);

    bql_unlock();
    if (ret < 0) {
        goto out;
    }

    if (migrate_auto_converge()) {
        mig_throttle_counter_reset();
    }
    qemu_savevm_live_state(s->to_dst_file);

    qemu_fflush(fb);

    colo_send_message_value(s->to_dst_file, COLO_MESSAGE_VMSTATE_SIZE,
                            bioc->usage, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_put_buffer(s->to_dst_file, bioc->data, bioc->usage);
    ret = qemu_fflush(s->to_dst_file);
    if (ret < 0) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_RECEIVED, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_event_reset(&s->colo_checkpoint_event);
    colo_notify_compares_event(NULL, COLO_EVENT_CHECKPOINT, &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_LOADED, &local_err);
    if (local_err) {
        goto out;
    }

    ret = 0;

    bql_lock();
    vm_start();
    bql_unlock();
    trace_colo_vm_state_change("stop", "run");

out:
    if (local_err) {
        error_report_err(local_err);
    }
    return ret;
}

static void colo_compare_notify_checkpoint(Notifier *notifier, void *data)
{
    colo_checkpoint_notify();
}

static void colo_process_checkpoint(MigrationState *s)
{
    QIOChannelBuffer *bioc;
    QEMUFile *fb = NULL;
    Error *local_err = NULL;
    int ret;

    if (get_colo_mode() != COLO_MODE_PRIMARY) {
        error_report("COLO mode must be COLO_MODE_PRIMARY");
        return;
    }

    failover_init_state();

    s->rp_state.from_dst_file = qemu_file_get_return_path(s->to_dst_file);
    if (!s->rp_state.from_dst_file) {
        error_report("Open QEMUFile from_dst_file failed");
        goto out;
    }

    packets_compare_notifier.notify = colo_compare_notify_checkpoint;
    colo_compare_register_notifier(&packets_compare_notifier);

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_CHECKPOINT_READY, &local_err);
    if (local_err) {
        goto out;
    }
    bioc = qio_channel_buffer_new(COLO_BUFFER_BASE_SIZE);
    fb = qemu_file_new_output(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    bql_lock();
    replication_start_all(REPLICATION_MODE_PRIMARY, &local_err);
    if (local_err) {
        bql_unlock();
        goto out;
    }

    vm_start();
    bql_unlock();
    trace_colo_vm_state_change("stop", "run");

    timer_mod(s->colo_delay_timer, qemu_clock_get_ms(QEMU_CLOCK_HOST) +
              migrate_checkpoint_delay());

    while (s->state == MIGRATION_STATUS_COLO) {
        if (failover_get_state() != FAILOVER_STATUS_NONE) {
            error_report("failover request");
            goto out;
        }

        qemu_event_wait(&s->colo_checkpoint_event);

        if (s->state != MIGRATION_STATUS_COLO) {
            goto out;
        }
        ret = colo_do_checkpoint_transaction(s, bioc, fb);
        if (ret < 0) {
            goto out;
        }
    }

out:
    if (local_err) {
        error_report_err(local_err);
    }

    if (fb) {
        qemu_fclose(fb);
    }

    switch (failover_get_state()) {
    case FAILOVER_STATUS_COMPLETED:
        qapi_event_send_colo_exit(COLO_MODE_PRIMARY,
                                  COLO_EXIT_REASON_REQUEST);
        break;
    default:
        qapi_event_send_colo_exit(COLO_MODE_PRIMARY,
                                  COLO_EXIT_REASON_ERROR);
    }

    qemu_event_wait(&s->colo_exit_event);
    qemu_event_destroy(&s->colo_exit_event);

    colo_compare_unregister_notifier(&packets_compare_notifier);
    timer_free(s->colo_delay_timer);
    qemu_event_destroy(&s->colo_checkpoint_event);

    if (s->rp_state.from_dst_file) {
        qemu_fclose(s->rp_state.from_dst_file);
        s->rp_state.from_dst_file = NULL;
    }
}

void migrate_start_colo_process(MigrationState *s)
{
    bql_unlock();
    qemu_event_init(&s->colo_checkpoint_event, false);
    s->colo_delay_timer =  timer_new_ms(QEMU_CLOCK_HOST,
                                colo_checkpoint_notify_timer, NULL);

    qemu_event_init(&s->colo_exit_event, false);
    colo_process_checkpoint(s);
    bql_lock();
}

static void colo_incoming_process_checkpoint(MigrationIncomingState *mis,
                      QEMUFile *fb, QIOChannelBuffer *bioc, Error **errp)
{
    uint64_t total_size;
    uint64_t value;
    Error *local_err = NULL;
    int ret;

    bql_lock();
    vm_stop_force_state(RUN_STATE_COLO);
    bql_unlock();
    trace_colo_vm_state_change("run", "stop");

    colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_REPLY,
                 &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    colo_receive_check_message(mis->from_src_file,
                       COLO_MESSAGE_VMSTATE_SEND, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    bql_lock();
    cpu_synchronize_all_states();
    ret = qemu_loadvm_state_main(mis->from_src_file, mis, errp);
    bql_unlock();

    if (ret < 0) {
        return;
    }

    value = colo_receive_message_value(mis->from_src_file,
                             COLO_MESSAGE_VMSTATE_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (value > bioc->capacity) {
        bioc->capacity = value;
        bioc->data = g_realloc(bioc->data, bioc->capacity);
    }
    total_size = qemu_get_buffer(mis->from_src_file, bioc->data, value);
    if (total_size != value) {
        error_setg(errp, "Got %" PRIu64 " VMState data, less than expected"
                    " %" PRIu64, total_size, value);
        return;
    }
    bioc->usage = total_size;
    qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);

    colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_RECEIVED,
                 &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    bql_lock();
    vmstate_loading = true;
    colo_flush_ram_cache();
    ret = qemu_load_device_state(fb, errp);
    if (ret < 0) {
        vmstate_loading = false;
        bql_unlock();
        return;
    }

    replication_get_error_all(&local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        vmstate_loading = false;
        bql_unlock();
        return;
    }

    replication_do_checkpoint_all(&local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        vmstate_loading = false;
        bql_unlock();
        return;
    }
    colo_notify_filters_event(COLO_EVENT_CHECKPOINT, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        vmstate_loading = false;
        bql_unlock();
        return;
    }

    vmstate_loading = false;
    vm_start();
    bql_unlock();
    trace_colo_vm_state_change("stop", "run");

    if (failover_get_state() == FAILOVER_STATUS_RELAUNCH) {
        return;
    }

    colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_LOADED,
                 &local_err);
    error_propagate(errp, local_err);
}

static void colo_wait_handle_message(MigrationIncomingState *mis,
                QEMUFile *fb, QIOChannelBuffer *bioc, Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(mis->from_src_file, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    switch (msg) {
    case COLO_MESSAGE_CHECKPOINT_REQUEST:
        colo_incoming_process_checkpoint(mis, fb, bioc, errp);
        break;
    default:
        error_setg(errp, "Got unknown COLO message: %d", msg);
        break;
    }
}

void colo_shutdown(void)
{
    MigrationIncomingState *mis = NULL;
    MigrationState *s = NULL;

    switch (get_colo_mode()) {
    case COLO_MODE_PRIMARY:
        s = migrate_get_current();
        qemu_event_set(&s->colo_checkpoint_event);
        qemu_event_set(&s->colo_exit_event);
        break;
    case COLO_MODE_SECONDARY:
        mis = migration_incoming_get_current();
        qemu_event_set(&mis->colo_incoming_event);
        break;
    default:
        break;
    }
}

static void *colo_process_incoming_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    QEMUFile *fb = NULL;
    QIOChannelBuffer *bioc = NULL;
    Error *local_err = NULL;

    rcu_register_thread();
    qemu_event_init(&mis->colo_incoming_event, false);

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);

    if (get_colo_mode() != COLO_MODE_SECONDARY) {
        error_report("COLO mode must be COLO_MODE_SECONDARY");
        return NULL;
    }

    bql_lock();
    migration_block_activate(&local_err);
    bql_unlock();
    if (local_err) {
        error_report_err(local_err);
        return NULL;
    }

    failover_init_state();

    mis->to_src_file = qemu_file_get_return_path(mis->from_src_file);
    if (!qemu_file_set_blocking(mis->from_src_file, true, &local_err)) {
        error_report_err(local_err);
        goto out;
    }

    colo_incoming_start_dirty_log();

    bioc = qio_channel_buffer_new(COLO_BUFFER_BASE_SIZE);
    fb = qemu_file_new_input(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    bql_lock();
    replication_start_all(REPLICATION_MODE_SECONDARY, &local_err);
    if (local_err) {
        bql_unlock();
        goto out;
    }
    vm_start();
    bql_unlock();
    trace_colo_vm_state_change("stop", "run");

    colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_READY,
                      &local_err);
    if (local_err) {
        goto out;
    }

    while (mis->state == MIGRATION_STATUS_COLO) {
        colo_wait_handle_message(mis, fb, bioc, &local_err);
        if (local_err) {
            error_report_err(local_err);
            break;
        }

        if (failover_get_state() == FAILOVER_STATUS_RELAUNCH) {
            failover_set_state(FAILOVER_STATUS_RELAUNCH,
                            FAILOVER_STATUS_NONE);
            failover_request_active(NULL);
            break;
        }

        if (failover_get_state() != FAILOVER_STATUS_NONE) {
            error_report("failover request");
            break;
        }
    }

out:
    switch (failover_get_state()) {
    case FAILOVER_STATUS_COMPLETED:
        qapi_event_send_colo_exit(COLO_MODE_SECONDARY,
                                  COLO_EXIT_REASON_REQUEST);
        break;
    default:
        qapi_event_send_colo_exit(COLO_MODE_SECONDARY,
                                  COLO_EXIT_REASON_ERROR);
    }

    if (fb) {
        qemu_fclose(fb);
    }

    qemu_event_wait(&mis->colo_incoming_event);
    qemu_event_destroy(&mis->colo_incoming_event);

    rcu_unregister_thread();
    return NULL;
}

void coroutine_fn colo_incoming_co(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    QemuThread th;

    assert(bql_locked());
    assert(migration_incoming_colo_enabled());

    qemu_thread_create(&th, MIGRATION_THREAD_DST_COLO,
                       colo_process_incoming_thread,
                       mis, QEMU_THREAD_JOINABLE);

    mis->colo_incoming_co = qemu_coroutine_self();
    qemu_coroutine_yield();
    mis->colo_incoming_co = NULL;

    bql_unlock();
    qemu_thread_join(&th);
    bql_lock();

    colo_release_ram_cache();
}
