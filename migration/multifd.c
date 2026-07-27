/*
 * Multifd common code
 *
 * Copyright (c) 2019-2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/iov.h"
#include "qemu/rcu.h"
#include "exec/target_page.h"
#include "system/system.h"
#include "system/ramblock.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "file.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "savevm.h"
#include "socket.h"
#include "tls.h"
#include "qemu-file.h"
#include "trace.h"
#include "multifd.h"
#include "threadinfo.h"
#include "options.h"
#include "qemu/yank.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "yank_functions.h"

typedef struct {
    uint32_t magic;
    uint32_t version;
    unsigned char uuid[16];
    uint8_t id;
    uint8_t unused1[7];
    uint64_t unused2[4];
} __attribute__((packed)) MultiFDInit_t;

struct {
    MultiFDSendParams *params;

    QemuMutex multifd_send_mutex;

    uintptr_t packet_num;
    QemuSemaphore channels_created;
    QemuSemaphore channels_ready;
    int exiting;
    const MultiFDMethods *ops;
} *multifd_send_state;

struct {
    MultiFDRecvParams *params;
    MultiFDRecvData *data;
    int count;
    QemuSemaphore sem_sync;
    uint64_t packet_num;
    int exiting;
    const MultiFDMethods *ops;
} *multifd_recv_state;

MultiFDSendData *multifd_send_data_alloc(void)
{
    MultiFDSendData *new = g_new0(MultiFDSendData, 1);

    multifd_ram_payload_alloc(&new->u.ram);

    return new;
}

void multifd_send_data_clear(MultiFDSendData *data)
{
    if (multifd_payload_empty(data)) {
        return;
    }

    switch (data->type) {
    case MULTIFD_PAYLOAD_DEVICE_STATE:
        multifd_send_data_clear_device_state(&data->u.device_state);
        break;
    default:
        break;
    }

    data->type = MULTIFD_PAYLOAD_NONE;
}

void multifd_send_data_free(MultiFDSendData *data)
{
    if (!data) {
        return;
    }

    multifd_send_data_clear(data);

    multifd_ram_payload_free(&data->u.ram);

    g_free(data);
}

static bool multifd_use_packets(void)
{
    return !migrate_mapped_ram();
}

void multifd_send_channel_created(void)
{
    qemu_sem_post(&multifd_send_state->channels_created);
}

static const MultiFDMethods *multifd_ops[MULTIFD_COMPRESSION__MAX] = {};

void multifd_register_ops(int method, const MultiFDMethods *ops)
{
    assert(0 <= method && method < MULTIFD_COMPRESSION__MAX);
    assert(!multifd_ops[method]);
    multifd_ops[method] = ops;
}

static int multifd_send_initial_packet(MultiFDSendParams *p, Error **errp)
{
    MultiFDInit_t msg = {};
    size_t size = sizeof(msg);
    int ret;

    msg.magic = cpu_to_be32(MULTIFD_MAGIC);
    msg.version = cpu_to_be32(MULTIFD_VERSION);
    msg.id = p->id;
    memcpy(msg.uuid, &qemu_uuid.data, sizeof(msg.uuid));

    ret = qio_channel_write_all(p->c, (char *)&msg, size, errp);
    if (ret != 0) {
        return -1;
    }
    stat64_add(&mig_stats.multifd_bytes, size);
    return 0;
}

static int multifd_recv_initial_packet(QIOChannel *c, Error **errp)
{
    MultiFDInit_t msg;
    int ret;

    ret = qio_channel_read_all(c, (char *)&msg, sizeof(msg), errp);
    if (ret != 0) {
        return -1;
    }

    msg.magic = be32_to_cpu(msg.magic);
    msg.version = be32_to_cpu(msg.version);

    if (msg.magic != MULTIFD_MAGIC) {
        error_setg(errp, "multifd: received packet magic %x "
                   "expected %x", msg.magic, MULTIFD_MAGIC);
        return -1;
    }

    if (msg.version != MULTIFD_VERSION) {
        error_setg(errp, "multifd: received packet version %u "
                   "expected %u", msg.version, MULTIFD_VERSION);
        return -1;
    }

    if (memcmp(msg.uuid, &qemu_uuid, sizeof(qemu_uuid))) {
        char *uuid = qemu_uuid_unparse_strdup(&qemu_uuid);
        char *msg_uuid = qemu_uuid_unparse_strdup((const QemuUUID *)msg.uuid);

        error_setg(errp, "multifd: received uuid '%s' and expected "
                   "uuid '%s' for channel %hhd", msg_uuid, uuid, msg.id);
        g_free(uuid);
        g_free(msg_uuid);
        return -1;
    }

    if (msg.id > migrate_multifd_channels()) {
        error_setg(errp, "multifd: received channel id %u is greater than "
                   "number of channels %u", msg.id, migrate_multifd_channels());
        return -1;
    }

    return msg.id;
}

void multifd_send_fill_packet(MultiFDSendParams *p)
{
    MultiFDPacket_t *packet = p->packet;
    uint64_t packet_num;
    bool sync_packet = p->flags & MULTIFD_FLAG_SYNC;

    memset(packet, 0, p->packet_len);

    packet->hdr.magic = cpu_to_be32(MULTIFD_MAGIC);
    packet->hdr.version = cpu_to_be32(MULTIFD_VERSION);

    packet->hdr.flags = cpu_to_be32(p->flags);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);

    packet_num = qatomic_fetch_inc(&multifd_send_state->packet_num);
    packet->packet_num = cpu_to_be64(packet_num);

    p->packets_sent++;

    if (!sync_packet) {
        multifd_ram_fill_packet(p);
    }

    trace_multifd_send_fill(p->id, packet_num,
                            p->flags, p->next_packet_size);
}

static int multifd_recv_unfill_packet_header(MultiFDRecvParams *p,
                                             const MultiFDPacketHdr_t *hdr,
                                             Error **errp)
{
    uint32_t magic = be32_to_cpu(hdr->magic);
    uint32_t version = be32_to_cpu(hdr->version);

    if (magic != MULTIFD_MAGIC) {
        error_setg(errp, "multifd: received packet magic %x, expected %x",
                   magic, MULTIFD_MAGIC);
        return -1;
    }

    if (version != MULTIFD_VERSION) {
        error_setg(errp, "multifd: received packet version %u, expected %u",
                   version, MULTIFD_VERSION);
        return -1;
    }

    p->flags = be32_to_cpu(hdr->flags);

    return 0;
}

static int multifd_recv_unfill_packet_device_state(MultiFDRecvParams *p,
                                                   Error **errp)
{
    MultiFDPacketDeviceState_t *packet = p->packet_dev_state;

    packet->instance_id = be32_to_cpu(packet->instance_id);
    p->next_packet_size = be32_to_cpu(packet->next_packet_size);

    return 0;
}

static int multifd_recv_unfill_packet_ram(MultiFDRecvParams *p, Error **errp)
{
    const MultiFDPacket_t *packet = p->packet;
    int ret = 0;

    p->next_packet_size = be32_to_cpu(packet->next_packet_size);
    p->packet_num = be64_to_cpu(packet->packet_num);

    ret = multifd_ram_unfill_packet(p, errp);

    trace_multifd_recv_unfill(p->id, p->packet_num, p->flags,
                              p->next_packet_size);

    return ret;
}

static int multifd_recv_unfill_packet(MultiFDRecvParams *p, Error **errp)
{
    p->packets_recved++;

    if (p->flags & MULTIFD_FLAG_DEVICE_STATE) {
        return multifd_recv_unfill_packet_device_state(p, errp);
    }

    return multifd_recv_unfill_packet_ram(p, errp);
}

static bool multifd_send_should_exit(void)
{
    return qatomic_read(&multifd_send_state->exiting);
}

static bool multifd_recv_should_exit(void)
{
    return qatomic_read(&multifd_recv_state->exiting);
}

static void multifd_send_kick_main(MultiFDSendParams *p)
{
    qemu_sem_post(&p->sem_sync);
    qemu_sem_post(&multifd_send_state->channels_ready);
}

bool multifd_send(MultiFDSendData **send_data)
{
    int i;
    static int next_channel;
    MultiFDSendParams *p = NULL;
    MultiFDSendData *tmp;

    if (multifd_send_should_exit()) {
        return false;
    }

    QEMU_LOCK_GUARD(&multifd_send_state->multifd_send_mutex);

    qemu_sem_wait(&multifd_send_state->channels_ready);

    next_channel %= migrate_multifd_channels();
    for (i = next_channel;; i = (i + 1) % migrate_multifd_channels()) {
        if (multifd_send_should_exit()) {
            return false;
        }
        p = &multifd_send_state->params[i];
        if (qatomic_read(&p->pending_job) == false) {
            next_channel = (i + 1) % migrate_multifd_channels();
            break;
        }
    }

    smp_mb_acquire();

    assert(multifd_payload_empty(p->data));

    tmp = *send_data;
    *send_data = p->data;
    p->data = tmp;

    qatomic_store_release(&p->pending_job, true);
    qemu_sem_post(&p->sem);

    return true;
}

static void multifd_send_set_error(Error *err)
{
    if (qatomic_xchg(&multifd_send_state->exiting, 1)) {
        return;
    }

    if (err) {
        MigrationState *s = migrate_get_current();
        migrate_set_error(s, err);
        if (s->state == MIGRATION_STATUS_SETUP ||
            s->state == MIGRATION_STATUS_PRE_SWITCHOVER ||
            s->state == MIGRATION_STATUS_DEVICE ||
            s->state == MIGRATION_STATUS_ACTIVE) {
            migrate_set_state(&s->state, s->state,
                              MIGRATION_STATUS_FAILED);
        }
    }
}

static void migration_ioc_shutdown_gracefully(QIOChannel *ioc)
{
    Error *local_err = NULL;

    if (!migration_has_failed(migrate_get_current()) &&
        object_dynamic_cast((Object *)ioc, TYPE_QIO_CHANNEL_TLS)) {

        migration_tls_channel_end(ioc, &local_err);
        if (local_err) {
            warn_reportf_err(local_err,
                        "Failed to gracefully terminate TLS connection: ");
        }
    }

    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}

static void multifd_send_terminate_threads(void)
{
    int i;

    trace_multifd_send_terminate_threads();

    qatomic_set(&multifd_send_state->exiting, 1);

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_sem_post(&p->sem);
        if (p->c) {
            migration_ioc_shutdown_gracefully(p->c);
        }
    }

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (p->tls_thread_created) {
            qemu_thread_join(&p->tls_thread);
        }

        if (p->thread_created) {
            qemu_thread_join(&p->thread);
        }
    }
}

static bool multifd_send_cleanup_channel(MultiFDSendParams *p, Error **errp)
{
    if (p->c) {
        migration_ioc_unregister_yank(p->c);
        qio_channel_close(p->c, &error_abort);
        object_unref(OBJECT(p->c));
        p->c = NULL;
    }
    qemu_sem_destroy(&p->sem);
    qemu_sem_destroy(&p->sem_sync);
    g_free(p->name);
    p->name = NULL;
    g_clear_pointer(&p->data, multifd_send_data_free);
    p->packet_len = 0;
    g_clear_pointer(&p->packet_device_state, g_free);
    g_free(p->packet);
    p->packet = NULL;
    multifd_send_state->ops->send_cleanup(p, errp);
    assert(!p->iov);

    return *errp == NULL;
}

static void multifd_send_cleanup_state(void)
{
    file_cleanup_outgoing_migration();
    socket_cleanup_outgoing_migration();
    multifd_device_state_send_cleanup();
    qemu_sem_destroy(&multifd_send_state->channels_created);
    qemu_sem_destroy(&multifd_send_state->channels_ready);
    qemu_mutex_destroy(&multifd_send_state->multifd_send_mutex);
    g_free(multifd_send_state->params);
    multifd_send_state->params = NULL;
    g_free(multifd_send_state);
    multifd_send_state = NULL;
}

void multifd_send_shutdown(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }

    multifd_send_terminate_threads();

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];
        Error *local_err = NULL;

        if (!multifd_send_cleanup_channel(p, &local_err)) {
            migrate_set_error(migrate_get_current(), local_err);
            error_free(local_err);
        }
    }

    multifd_send_cleanup_state();
}

static int multifd_zero_copy_flush(QIOChannel *c)
{
    int ret;
    Error *err = NULL;

    ret = qio_channel_flush(c, &err);
    if (ret < 0) {
        error_report_err(err);
        return -1;
    }
    if (ret == 1) {
        stat64_add(&mig_stats.dirty_sync_missed_zero_copy, 1);
    }

    return ret;
}

int multifd_send_sync_main(MultiFDSyncReq req)
{
    int i;
    bool flush_zero_copy;

    assert(req != MULTIFD_SYNC_NONE);

    flush_zero_copy = migrate_zero_copy_send();

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (multifd_send_should_exit()) {
            return -1;
        }

        trace_multifd_send_sync_main_signal(p->id);

        assert(qatomic_read(&p->pending_sync) == MULTIFD_SYNC_NONE);
        qatomic_set(&p->pending_sync, req);
        qemu_sem_post(&p->sem);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (multifd_send_should_exit()) {
            return -1;
        }

        qemu_sem_wait(&multifd_send_state->channels_ready);
        trace_multifd_send_sync_main_wait(p->id);
        qemu_sem_wait(&p->sem_sync);

        if (flush_zero_copy && p->c && (multifd_zero_copy_flush(p->c) < 0)) {
            return -1;
        }
    }
    trace_multifd_send_sync_main(multifd_send_state->packet_num);

    return 0;
}

static void *multifd_send_thread(void *opaque)
{
    MultiFDSendParams *p = opaque;
    MigrationThread *thread = NULL;
    Error *local_err = NULL;
    int ret = 0;
    bool use_packets = multifd_use_packets();

    thread = migration_threads_add(p->name, qemu_get_thread_id());

    trace_multifd_send_thread_start(p->id);
    rcu_register_thread();

    if (use_packets) {
        if (multifd_send_initial_packet(p, &local_err) < 0) {
            ret = -1;
            goto out;
        }
    }

    while (true) {
        qemu_sem_post(&multifd_send_state->channels_ready);
        qemu_sem_wait(&p->sem);

        if (multifd_send_should_exit()) {
            break;
        }

        if (qatomic_load_acquire(&p->pending_job)) {
            bool is_device_state = multifd_payload_device_state(p->data);
            size_t total_size;
            int write_flags_masked = 0;

            p->flags = 0;
            p->iovs_num = 0;
            assert(!multifd_payload_empty(p->data));

            if (is_device_state) {
                multifd_device_state_send_prepare(p);

                write_flags_masked |= QIO_CHANNEL_WRITE_FLAG_ZERO_COPY;
            } else {
                ret = multifd_send_state->ops->send_prepare(p, &local_err);
                if (ret != 0) {
                    break;
                }
            }

            total_size = iov_size(p->iov, p->iovs_num);

            if (migrate_mapped_ram()) {
                assert(!is_device_state);

                ret = file_write_ramblock_iov(p->c, p->iov, p->iovs_num,
                                              &p->data->u.ram, &local_err);
            } else {
                ret = qio_channel_writev_full_all(p->c, p->iov, p->iovs_num,
                                                  NULL, 0,
                                                  p->write_flags & ~write_flags_masked,
                                                  &local_err);
            }

            if (ret != 0) {
                break;
            }

            stat64_add(&mig_stats.multifd_bytes, total_size);

            p->next_packet_size = 0;
            multifd_send_data_clear(p->data);

            qatomic_store_release(&p->pending_job, false);
        } else {
            MultiFDSyncReq req = qatomic_read(&p->pending_sync);

            assert(req != MULTIFD_SYNC_NONE);

            if (req == MULTIFD_SYNC_ALL) {
                p->flags = MULTIFD_FLAG_SYNC;
                multifd_send_fill_packet(p);
                ret = qio_channel_write_all(p->c, (void *)p->packet,
                                            p->packet_len, &local_err);
                if (ret != 0) {
                    break;
                }
                stat64_add(&mig_stats.multifd_bytes, p->packet_len);
            }

            qatomic_set(&p->pending_sync, MULTIFD_SYNC_NONE);
            qemu_sem_post(&p->sem_sync);
        }
    }

out:
    if (ret) {
        assert(local_err);
        trace_multifd_send_error(p->id);
        multifd_send_set_error(local_err);
        multifd_send_kick_main(p);
        error_free(local_err);
    }

    rcu_unregister_thread();
    migration_threads_remove(thread);
    trace_multifd_send_thread_end(p->id, p->packets_sent);

    return NULL;
}

static void multifd_new_send_channel_async(QIOTask *task, gpointer opaque);

typedef struct {
    MultiFDSendParams *p;
    QIOChannelTLS *tioc;
} MultiFDTLSThreadArgs;

static void *multifd_tls_handshake_thread(void *opaque)
{
    MultiFDTLSThreadArgs *args = opaque;

    qio_channel_tls_handshake(args->tioc,
                              multifd_new_send_channel_async,
                              args->p,
                              NULL,
                              NULL);
    g_free(args);

    return NULL;
}

static bool multifd_tls_channel_connect(MultiFDSendParams *p,
                                        QIOChannel *ioc,
                                        Error **errp)
{
    MigrationState *s = migrate_get_current();
    const char *hostname = s->hostname;
    MultiFDTLSThreadArgs *args;
    QIOChannelTLS *tioc;

    tioc = migration_tls_client_create(ioc, hostname, errp);
    if (!tioc) {
        return false;
    }

    object_unref(OBJECT(ioc));
    trace_multifd_tls_outgoing_handshake_start(ioc, tioc, hostname);
    qio_channel_set_name(QIO_CHANNEL(tioc), "multifd-tls-outgoing");

    args = g_new0(MultiFDTLSThreadArgs, 1);
    args->tioc = tioc;
    args->p = p;

    p->tls_thread_created = true;
    qemu_thread_create(&p->tls_thread, MIGRATION_THREAD_SRC_TLS,
                       multifd_tls_handshake_thread, args,
                       QEMU_THREAD_JOINABLE);
    return true;
}

void multifd_channel_connect(MultiFDSendParams *p, QIOChannel *ioc)
{
    qio_channel_set_delay(ioc, false);

    migration_ioc_register_yank(ioc);
    p->c = ioc;

    p->thread_created = true;
    qemu_thread_create(&p->thread, p->name, multifd_send_thread, p,
                       QEMU_THREAD_JOINABLE);
}

static void multifd_new_send_channel_async(QIOTask *task, gpointer opaque)
{
    MultiFDSendParams *p = opaque;
    QIOChannel *ioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *local_err = NULL;
    bool ret;

    trace_multifd_new_send_channel_async(p->id);

    if (qio_task_propagate_error(task, &local_err)) {
        ret = false;
        goto out;
    }

    trace_multifd_set_outgoing_channel(ioc, object_get_typename(OBJECT(ioc)),
                                       migrate_get_current()->hostname);

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        ret = multifd_tls_channel_connect(p, ioc, &local_err);
        if (ret) {
            return;
        }
    } else {
        multifd_channel_connect(p, ioc);
        ret = true;
    }

out:
    multifd_send_channel_created();

    if (ret) {
        return;
    }

    trace_multifd_new_send_channel_async_error(p->id, local_err);
    multifd_send_set_error(local_err);
    object_unref(OBJECT(ioc));
    error_free(local_err);
}

static bool multifd_new_send_channel_create(gpointer opaque, Error **errp)
{
    if (!multifd_use_packets()) {
        return file_send_channel_create(opaque, errp);
    }

    socket_send_channel_create(multifd_new_send_channel_async, opaque);
    return true;
}

bool multifd_send_setup(void)
{
    MigrationState *s = migrate_get_current();
    int thread_count, ret = 0;
    uint32_t page_count = multifd_ram_page_count();
    bool use_packets = multifd_use_packets();
    uint8_t i;

    if (!migrate_multifd()) {
        return true;
    }

    thread_count = migrate_multifd_channels();
    multifd_send_state = g_malloc0(sizeof(*multifd_send_state));
    multifd_send_state->params = g_new0(MultiFDSendParams, thread_count);
    qemu_mutex_init(&multifd_send_state->multifd_send_mutex);
    qemu_sem_init(&multifd_send_state->channels_created, 0);
    qemu_sem_init(&multifd_send_state->channels_ready, 0);
    qatomic_set(&multifd_send_state->exiting, 0);
    multifd_send_state->ops = multifd_ops[migrate_multifd_compression()];

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];
        Error *local_err = NULL;

        qemu_sem_init(&p->sem, 0);
        qemu_sem_init(&p->sem_sync, 0);
        p->id = i;
        p->data = multifd_send_data_alloc();

        if (use_packets) {
            p->packet_len = sizeof(MultiFDPacket_t)
                          + sizeof(uint64_t) * page_count;
            p->packet = g_malloc0(p->packet_len);
            p->packet_device_state = g_malloc0(sizeof(*p->packet_device_state));
            p->packet_device_state->hdr.magic = cpu_to_be32(MULTIFD_MAGIC);
            p->packet_device_state->hdr.version = cpu_to_be32(MULTIFD_VERSION);
        }
        p->name = g_strdup_printf(MIGRATION_THREAD_SRC_MULTIFD, i);
        p->write_flags = 0;

        if (!multifd_new_send_channel_create(p, &local_err)) {
            migrate_set_error(s, local_err);
            error_free(local_err);
            ret = -1;
        }
    }

    for (i = 0; i < thread_count; i++) {
        qemu_sem_wait(&multifd_send_state->channels_created);
    }

    if (ret) {
        goto err;
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];
        Error *local_err = NULL;

        ret = multifd_send_state->ops->send_setup(p, &local_err);
        if (ret) {
            migrate_set_error(s, local_err);
            error_free(local_err);
            goto err;
        }
        assert(p->iov);
    }

    multifd_device_state_send_setup();

    return true;

err:
    migrate_set_state(&s->state, MIGRATION_STATUS_SETUP,
                      MIGRATION_STATUS_FAILED);
    return false;
}

bool multifd_recv(void)
{
    int i;
    static int next_recv_channel;
    MultiFDRecvParams *p = NULL;
    MultiFDRecvData *data = multifd_recv_state->data;

    next_recv_channel %= migrate_multifd_channels();
    for (i = next_recv_channel;; i = (i + 1) % migrate_multifd_channels()) {
        if (multifd_recv_should_exit()) {
            return false;
        }

        p = &multifd_recv_state->params[i];

        if (qatomic_read(&p->pending_job) == false) {
            next_recv_channel = (i + 1) % migrate_multifd_channels();
            break;
        }
    }

    smp_mb_acquire();

    assert(!p->data->size);
    multifd_recv_state->data = p->data;
    p->data = data;

    qatomic_store_release(&p->pending_job, true);
    qemu_sem_post(&p->sem);

    return true;
}

MultiFDRecvData *multifd_get_recv_data(void)
{
    return multifd_recv_state->data;
}

static void multifd_recv_terminate_threads(Error *err)
{
    int i;

    trace_multifd_recv_terminate_threads(err != NULL);

    if (qatomic_xchg(&multifd_recv_state->exiting, 1)) {
        return;
    }

    if (err) {
        MigrationState *s = migrate_get_current();
        migrate_set_error(s, err);
        if (s->state == MIGRATION_STATUS_SETUP ||
            s->state == MIGRATION_STATUS_ACTIVE) {
            migrate_set_state(&s->state, s->state,
                              MIGRATION_STATUS_FAILED);
        }
    }

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        if (multifd_use_packets()) {
            qemu_sem_post(&p->sem_sync);
        } else {
            qemu_sem_post(&p->sem);
        }

        if (p->c) {
            qio_channel_shutdown(p->c, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
    }
}

void multifd_recv_shutdown(void)
{
    if (migrate_multifd()) {
        multifd_recv_terminate_threads(NULL);
    }
}

static void multifd_recv_cleanup_channel(MultiFDRecvParams *p)
{
    migration_ioc_unregister_yank(p->c);
    object_unref(OBJECT(p->c));
    p->c = NULL;
    qemu_mutex_destroy(&p->mutex);
    qemu_sem_destroy(&p->sem_sync);
    qemu_sem_destroy(&p->sem);
    g_free(p->data);
    p->data = NULL;
    g_free(p->name);
    p->name = NULL;
    p->packet_len = 0;
    g_free(p->packet);
    p->packet = NULL;
    g_clear_pointer(&p->packet_dev_state, g_free);
    g_free(p->normal);
    p->normal = NULL;
    g_free(p->zero);
    p->zero = NULL;
    multifd_recv_state->ops->recv_cleanup(p);
}

static void multifd_recv_cleanup_state(void)
{
    qemu_sem_destroy(&multifd_recv_state->sem_sync);
    g_free(multifd_recv_state->params);
    multifd_recv_state->params = NULL;
    g_free(multifd_recv_state->data);
    multifd_recv_state->data = NULL;
    g_free(multifd_recv_state);
    multifd_recv_state = NULL;
}

void multifd_recv_cleanup(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }
    multifd_recv_terminate_threads(NULL);
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        if (p->thread_created) {
            qemu_thread_join(&p->thread);
        }
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        multifd_recv_cleanup_channel(&multifd_recv_state->params[i]);
    }
    multifd_recv_cleanup_state();
}

void multifd_recv_sync_main(void)
{
    int thread_count = migrate_multifd_channels();
    bool file_based = !multifd_use_packets();
    int i;

    if (!migrate_multifd()) {
        return;
    }

    if (file_based) {
        for (i = 0; i < thread_count; i++) {
            MultiFDRecvParams *p = &multifd_recv_state->params[i];

            trace_multifd_recv_sync_main_signal(p->id);
            qemu_sem_post(&p->sem);
        }
    }

    for (i = 0; i < thread_count; i++) {
        trace_multifd_recv_sync_main_wait(i);
        qemu_sem_wait(&multifd_recv_state->sem_sync);
    }

    if (file_based) {
        return;
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        WITH_QEMU_LOCK_GUARD(&p->mutex) {
            if (multifd_recv_state->packet_num < p->packet_num) {
                multifd_recv_state->packet_num = p->packet_num;
            }
        }
        trace_multifd_recv_sync_main_signal(p->id);
        qemu_sem_post(&p->sem_sync);
    }
    trace_multifd_recv_sync_main(multifd_recv_state->packet_num);
}

static int multifd_device_state_recv(MultiFDRecvParams *p, Error **errp)
{
    g_autofree char *dev_state_buf = NULL;
    int ret;

    dev_state_buf = g_malloc(p->next_packet_size);

    ret = qio_channel_read_all(p->c, dev_state_buf, p->next_packet_size, errp);
    if (ret != 0) {
        return ret;
    }

    if (p->packet_dev_state->idstr[sizeof(p->packet_dev_state->idstr) - 1]
        != 0) {
        error_setg(errp, "unterminated multifd device state idstr");
        return -1;
    }

    if (!qemu_loadvm_load_state_buffer(p->packet_dev_state->idstr,
                                       p->packet_dev_state->instance_id,
                                       dev_state_buf, p->next_packet_size,
                                       errp)) {
        ret = -1;
    }

    return ret;
}

static void *multifd_recv_thread(void *opaque)
{
    MigrationState *s = migrate_get_current();
    MultiFDRecvParams *p = opaque;
    Error *local_err = NULL;
    bool use_packets = multifd_use_packets();
    int ret;

    trace_multifd_recv_thread_start(p->id);
    rcu_register_thread();

    if (!s->multifd_clean_tls_termination) {
        p->read_flags = QIO_CHANNEL_READ_FLAG_RELAXED_EOF;
    }

    while (true) {
        MultiFDPacketHdr_t hdr;
        uint32_t flags = 0;
        bool is_device_state = false;
        bool has_data = false;
        uint8_t *pkt_buf;
        size_t pkt_len;

        p->normal_num = 0;

        if (use_packets) {
            struct iovec iov = {
                .iov_base = (void *)&hdr,
                .iov_len = sizeof(hdr)
            };

            if (multifd_recv_should_exit()) {
                break;
            }

            ret = qio_channel_readv_full_all_eof(p->c, &iov, 1, NULL, NULL,
                                                 p->read_flags, &local_err);
            if (!ret) {
                assert(!local_err);
                break;
            }

            if (ret == -1) {
                break;
            }

            ret = multifd_recv_unfill_packet_header(p, &hdr, &local_err);
            if (ret) {
                break;
            }

            is_device_state = p->flags & MULTIFD_FLAG_DEVICE_STATE;
            if (is_device_state) {
                pkt_buf = (uint8_t *)p->packet_dev_state + sizeof(hdr);
                pkt_len = sizeof(*p->packet_dev_state) - sizeof(hdr);
            } else {
                pkt_buf = (uint8_t *)p->packet + sizeof(hdr);
                pkt_len = p->packet_len - sizeof(hdr);
            }

            ret = qio_channel_read_all_eof(p->c, (char *)pkt_buf, pkt_len,
                                           &local_err);
            if (!ret) {
                error_setg(&local_err, "multifd: unexpected EOF after packet header");
                break;
            }

            if (ret == -1) {
                break;
            }

            qemu_mutex_lock(&p->mutex);
            ret = multifd_recv_unfill_packet(p, &local_err);
            if (ret) {
                qemu_mutex_unlock(&p->mutex);
                break;
            }

            flags = p->flags;
            p->flags &= ~MULTIFD_FLAG_SYNC;

            if (is_device_state) {
                has_data = p->next_packet_size > 0;
            } else {
                has_data = p->normal_num || p->zero_num;
            }

            qemu_mutex_unlock(&p->mutex);
        } else {
            qemu_sem_wait(&p->sem);

            if (multifd_recv_should_exit()) {
                break;
            }

            if (!qatomic_load_acquire(&p->pending_job)) {
                qemu_sem_post(&multifd_recv_state->sem_sync);
                continue;
            }

            has_data = !!p->data->size;
        }

        if (has_data) {
            assert(!migration_in_postcopy());
            if (is_device_state) {
                assert(use_packets);
                ret = multifd_device_state_recv(p, &local_err);
            } else {
                ret = multifd_recv_state->ops->recv(p, &local_err);
            }
            if (ret != 0) {
                break;
            }
        } else if (is_device_state) {
            error_setg(&local_err,
                       "multifd: received empty device state packet");
            break;
        }

        if (use_packets) {
            if (flags & MULTIFD_FLAG_SYNC) {
                if (is_device_state) {
                    error_setg(&local_err,
                               "multifd: received SYNC device state packet");
                    break;
                }

                qemu_sem_post(&multifd_recv_state->sem_sync);
                qemu_sem_wait(&p->sem_sync);
            }
        } else {
            p->data->size = 0;
            qatomic_store_release(&p->pending_job, false);
        }
    }

    if (local_err) {
        multifd_recv_terminate_threads(local_err);
        error_free(local_err);
    }

    rcu_unregister_thread();
    trace_multifd_recv_thread_end(p->id, p->packets_recved);

    return NULL;
}

int multifd_recv_setup(Error **errp)
{
    int thread_count;
    uint32_t page_count = multifd_ram_page_count();
    bool use_packets = multifd_use_packets();
    uint8_t i;

    if (multifd_recv_state || !migrate_multifd()) {
        return 0;
    }

    thread_count = migrate_multifd_channels();
    multifd_recv_state = g_malloc0(sizeof(*multifd_recv_state));
    multifd_recv_state->params = g_new0(MultiFDRecvParams, thread_count);

    multifd_recv_state->data = g_new0(MultiFDRecvData, 1);
    multifd_recv_state->data->size = 0;

    qatomic_set(&multifd_recv_state->count, 0);
    qatomic_set(&multifd_recv_state->exiting, 0);
    qemu_sem_init(&multifd_recv_state->sem_sync, 0);
    multifd_recv_state->ops = multifd_ops[migrate_multifd_compression()];

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        qemu_mutex_init(&p->mutex);
        qemu_sem_init(&p->sem_sync, 0);
        qemu_sem_init(&p->sem, 0);
        p->pending_job = false;
        p->id = i;

        p->data = g_new0(MultiFDRecvData, 1);
        p->data->size = 0;

        if (use_packets) {
            p->packet_len = sizeof(MultiFDPacket_t)
                + sizeof(uint64_t) * page_count;
            p->packet = g_malloc0(p->packet_len);
            p->packet_dev_state = g_malloc0(sizeof(*p->packet_dev_state));
        }
        p->name = g_strdup_printf(MIGRATION_THREAD_DST_MULTIFD, i);
        p->normal = g_new0(ram_addr_t, page_count);
        p->zero = g_new0(ram_addr_t, page_count);
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];
        int ret;

        ret = multifd_recv_state->ops->recv_setup(p, errp);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

bool multifd_recv_all_channels_created(void)
{
    int thread_count = migrate_multifd_channels();

    if (!migrate_multifd()) {
        return true;
    }

    if (!multifd_recv_state) {
        return false;
    }

    return thread_count == qatomic_read(&multifd_recv_state->count);
}

void multifd_recv_new_channel(QIOChannel *ioc, Error **errp)
{
    MultiFDRecvParams *p;
    Error *local_err = NULL;
    bool use_packets = multifd_use_packets();
    int id;

    if (use_packets) {
        id = multifd_recv_initial_packet(ioc, &local_err);
        if (id < 0) {
            multifd_recv_terminate_threads(local_err);
            error_propagate_prepend(errp, local_err,
                                    "failed to receive packet"
                                    " via multifd channel %d: ",
                                    qatomic_read(&multifd_recv_state->count));
            return;
        }
        trace_multifd_recv_new_channel(id);
    } else {
        id = qatomic_read(&multifd_recv_state->count);
    }

    p = &multifd_recv_state->params[id];
    if (p->c != NULL) {
        error_setg(&local_err, "multifd: received id '%d' already setup'",
                   id);
        multifd_recv_terminate_threads(local_err);
        error_propagate(errp, local_err);
        return;
    }
    p->c = ioc;
    object_ref(OBJECT(ioc));

    p->thread_created = true;
    qemu_thread_create(&p->thread, p->name, multifd_recv_thread, p,
                       QEMU_THREAD_JOINABLE);
    qatomic_inc(&multifd_recv_state->count);
}
