/*
 * QEMU live migration channel operations
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "channel.h"
#include "tls.h"
#include "migration.h"
#include "qemu-file.h"
#include "trace.h"
#include "qapi/error.h"
#include "io/channel-tls.h"
#include "io/channel-socket.h"
#include "qemu/yank.h"
#include "yank_functions.h"

void migration_channel_process_incoming(QIOChannel *ioc)
{
    MigrationState *s = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();
    Error *local_err = NULL;

    trace_migration_set_incoming_channel(
        ioc, object_get_typename(OBJECT(ioc)));

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        migration_tls_channel_process_incoming(s, ioc, &local_err);
    } else {
        migration_ioc_register_yank(ioc);
        migration_ioc_process_incoming(ioc, &local_err);
    }

    if (local_err) {
        error_report_err(local_err);
        migrate_set_state(&s->state, s->state, MIGRATION_STATUS_FAILED);
        if (mis->exit_on_error) {
            exit(EXIT_FAILURE);
        }
    }
}


void migration_channel_connect(MigrationState *s,
                               QIOChannel *ioc,
                               const char *hostname,
                               Error *error)
{
    trace_migration_set_outgoing_channel(
        ioc, object_get_typename(OBJECT(ioc)), hostname, error);

    if (!error) {
        if (migrate_channel_requires_tls_upgrade(ioc)) {
            migration_tls_channel_connect(s, ioc, hostname, &error);

            if (!error) {

                return;
            }
        } else {
            QEMUFile *f = qemu_file_new_output(ioc);

            migration_ioc_register_yank(ioc);

            qemu_mutex_lock(&s->qemu_file_lock);
            s->to_dst_file = f;
            qemu_mutex_unlock(&s->qemu_file_lock);
        }
    }
    migration_connect(s, error);
    error_free(error);
}


int migration_channel_read_peek(QIOChannel *ioc,
                                const char *buf,
                                const size_t buflen,
                                Error **errp)
{
    ssize_t len = 0;
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = buflen };

    while (true) {
        len = qio_channel_readv_full(ioc, &iov, 1, NULL, NULL,
                                     QIO_CHANNEL_READ_FLAG_MSG_PEEK, errp);

        if (len < 0 && len != QIO_CHANNEL_ERR_BLOCK) {
            return -1;
        }

        if (len == 0) {
            error_setg(errp, "Failed to peek at channel");
            return -1;
        }

        if (len == buflen) {
            break;
        }

        qio_channel_wait_cond(ioc, G_IO_IN);
    }

    return 0;
}
