/*
 * QEMU Block driver for NBD
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
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
#include "trace.h"

#include "block/nbd.h"

#include "qapi/qapi-visit-sockets.h"
#include "qapi/clone-visitor.h"
#include "qemu/coroutine.h"

#include "nbd/nbd-internal.h"

struct NBDClientConnection {
    SocketAddress *saddr;
    QCryptoTLSCreds *tlscreds;
    char *tlshostname;
    NBDExportInfo initial_info;
    bool do_negotiation;
    bool do_retry;

    QemuMutex mutex;

    NBDExportInfo updated_info;
    QIOChannelSocket *sioc;
    QIOChannel *ioc;
    Error *err;

    bool running;
    bool detached;

    Coroutine *wait_co;
};

void nbd_client_connection_enable_retry(NBDClientConnection *conn)
{
    conn->do_retry = true;
}

NBDClientConnection *nbd_client_connection_new(const SocketAddress *saddr,
                                               bool do_negotiation,
                                               const char *export_name,
                                               const char *x_dirty_bitmap,
                                               QCryptoTLSCreds *tlscreds,
                                               const char *tlshostname)
{
    NBDClientConnection *conn = g_new(NBDClientConnection, 1);

    object_ref(OBJECT(tlscreds));
    *conn = (NBDClientConnection) {
        .saddr = QAPI_CLONE(SocketAddress, saddr),
        .tlscreds = tlscreds,
        .tlshostname = g_strdup(tlshostname),
        .do_negotiation = do_negotiation,

        .initial_info.request_sizes = true,
        .initial_info.mode = NBD_MODE_EXTENDED,
        .initial_info.base_allocation = true,
        .initial_info.x_dirty_bitmap = g_strdup(x_dirty_bitmap),
        .initial_info.name = g_strdup(export_name ?: "")
    };

    qemu_mutex_init(&conn->mutex);

    return conn;
}

static void nbd_client_connection_do_free(NBDClientConnection *conn)
{
    if (conn->sioc) {
        qio_channel_close(QIO_CHANNEL(conn->sioc), NULL);
        object_unref(OBJECT(conn->sioc));
    }
    error_free(conn->err);
    qapi_free_SocketAddress(conn->saddr);
    g_free(conn->tlshostname);
    object_unref(OBJECT(conn->tlscreds));
    g_free(conn->initial_info.x_dirty_bitmap);
    g_free(conn->initial_info.name);
    g_free(conn);
}

static int nbd_connect(QIOChannelSocket *sioc, SocketAddress *addr,
                       NBDExportInfo *info, QCryptoTLSCreds *tlscreds,
                       const char *tlshostname,
                       QIOChannel **outioc, Error **errp)
{
    int ret;

    if (outioc) {
        *outioc = NULL;
    }

    ret = qio_channel_socket_connect_sync(sioc, addr, errp);
    if (ret < 0) {
        return ret;
    }

    nbd_set_socket_send_buffer(sioc);
    qio_channel_set_delay(QIO_CHANNEL(sioc), false);

    if (!info) {
        return 0;
    }

    ret = nbd_receive_negotiate(QIO_CHANNEL(sioc), tlscreds, tlshostname,
                                outioc, info, errp);
    if (ret < 0) {
        if (outioc && *outioc) {
            qio_channel_close(*outioc, NULL);
            object_unref(OBJECT(*outioc));
            *outioc = NULL;
        } else {
            qio_channel_close(QIO_CHANNEL(sioc), NULL);
        }

        return ret;
    }

    return 0;
}

static void *connect_thread_func(void *opaque)
{
    NBDClientConnection *conn = opaque;
    int ret;
    bool do_free;
    uint64_t timeout = 1;
    uint64_t max_timeout = 16;

    qemu_mutex_lock(&conn->mutex);
    while (!conn->detached) {
        Error *local_err = NULL;

        assert(!conn->sioc);
        conn->sioc = qio_channel_socket_new();

        qemu_mutex_unlock(&conn->mutex);

        conn->updated_info = conn->initial_info;

        ret = nbd_connect(conn->sioc, conn->saddr,
                          conn->do_negotiation ? &conn->updated_info : NULL,
                          conn->tlscreds, conn->tlshostname,
                          &conn->ioc, &local_err);

        conn->updated_info.x_dirty_bitmap = NULL;
        conn->updated_info.name = NULL;

        qemu_mutex_lock(&conn->mutex);

        error_free(conn->err);
        conn->err = NULL;
        error_propagate(&conn->err, local_err);

        if (ret < 0) {
            object_unref(OBJECT(conn->sioc));
            conn->sioc = NULL;
            if (conn->do_retry && !conn->detached) {
                trace_nbd_connect_thread_sleep(timeout);
                qemu_mutex_unlock(&conn->mutex);

                sleep(timeout);
                if (timeout < max_timeout) {
                    timeout *= 2;
                }

                qemu_mutex_lock(&conn->mutex);
                continue;
            }
        }

        break;
    }

    assert(conn->running);
    conn->running = false;
    if (conn->wait_co) {
        aio_co_wake(conn->wait_co);
        conn->wait_co = NULL;
    }
    do_free = conn->detached;

    qemu_mutex_unlock(&conn->mutex);

    if (do_free) {
        nbd_client_connection_do_free(conn);
    }

    return NULL;
}

void nbd_client_connection_release(NBDClientConnection *conn)
{
    bool do_free = false;

    if (!conn) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        assert(!conn->detached);
        if (conn->running) {
            conn->detached = true;
        } else {
            do_free = true;
        }
        if (conn->sioc) {
            qio_channel_shutdown(QIO_CHANNEL(conn->sioc),
                                 QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
    }

    if (do_free) {
        nbd_client_connection_do_free(conn);
    }
}

QIOChannel *coroutine_fn
nbd_co_establish_connection(NBDClientConnection *conn, NBDExportInfo *info,
                            bool blocking, Error **errp)
{
    QemuThread thread;

    if (conn->do_negotiation) {
        assert(info);
    }

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        assert(!conn->wait_co);

        if (!conn->running) {
            if (conn->sioc) {
                if (conn->do_negotiation) {
                    memcpy(info, &conn->updated_info, sizeof(*info));
                    if (conn->ioc) {
                        object_unref(OBJECT(conn->sioc));
                        conn->sioc = NULL;

                        return g_steal_pointer(&conn->ioc);
                    }
                }

                assert(!conn->ioc);

                return QIO_CHANNEL(g_steal_pointer(&conn->sioc));
            }

            conn->running = true;
            qemu_thread_create(&thread, "nbd-connect",
                               connect_thread_func, conn, QEMU_THREAD_DETACHED);
        }

        if (!blocking) {
            if (conn->err) {
                error_propagate(errp, error_copy(conn->err));
            } else {
                error_setg(errp, "No connection at the moment");
            }

            return NULL;
        }

        conn->wait_co = qemu_coroutine_self();
    }

    qemu_coroutine_yield();

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        if (conn->running) {
            if (conn->err) {
                error_propagate(errp, error_copy(conn->err));
            } else {
                error_setg(errp, "Connection attempt cancelled by timeout");
            }

            return NULL;
        } else {
            assert(!conn->err != !conn->sioc);

            if (conn->err) {
                error_propagate(errp, error_copy(conn->err));
                return NULL;
            }

            if (conn->do_negotiation) {
                memcpy(info, &conn->updated_info, sizeof(*info));
                if (conn->ioc) {
                    object_unref(OBJECT(conn->sioc));
                    conn->sioc = NULL;

                    return g_steal_pointer(&conn->ioc);
                }
            }

            assert(!conn->ioc);

            return QIO_CHANNEL(g_steal_pointer(&conn->sioc));
        }
    }

    abort();
}

void nbd_co_establish_connection_cancel(NBDClientConnection *conn)
{
    Coroutine *wait_co = NULL;

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        wait_co = g_steal_pointer(&conn->wait_co);
    }

    if (wait_co) {
        aio_co_wake(wait_co);
    }
}
