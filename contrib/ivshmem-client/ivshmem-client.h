/*
 * Copyright 6WIND S.A., 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef IVSHMEM_CLIENT_H
#define IVSHMEM_CLIENT_H

#include <sys/select.h>

#include "qemu/queue.h"
#include "hw/misc/ivshmem.h"

#define IVSHMEM_CLIENT_MAX_VECTORS 64

typedef struct IvshmemClientPeer {
    QTAILQ_ENTRY(IvshmemClientPeer) next;
    int64_t id;
    int vectors[IVSHMEM_CLIENT_MAX_VECTORS];
    unsigned vectors_count;
} IvshmemClientPeer;

typedef struct IvshmemClient IvshmemClient;

typedef void (*IvshmemClientNotifCb)(
    const IvshmemClient *client,
    const IvshmemClientPeer *peer,
    unsigned vect, void *arg);

struct IvshmemClient {
    char unix_sock_path[PATH_MAX];
    int sock_fd;
    int shm_fd;

    QTAILQ_HEAD(, IvshmemClientPeer) peer_list;
    IvshmemClientPeer local;

    IvshmemClientNotifCb notif_cb;
    void *notif_arg;

    bool verbose;
};

int ivshmem_client_init(IvshmemClient *client, const char *unix_sock_path,
                        IvshmemClientNotifCb notif_cb, void *notif_arg,
                        bool verbose);

int ivshmem_client_connect(IvshmemClient *client);

void ivshmem_client_close(IvshmemClient *client);

void ivshmem_client_get_fds(const IvshmemClient *client, fd_set *fds,
                            int *maxfd);

int ivshmem_client_handle_fds(IvshmemClient *client, fd_set *fds, int maxfd);

int ivshmem_client_notify(const IvshmemClient *client,
                          const IvshmemClientPeer *peer, unsigned vector);

int ivshmem_client_notify_all_vects(const IvshmemClient *client,
                                    const IvshmemClientPeer *peer);

int ivshmem_client_notify_broadcast(const IvshmemClient *client);

IvshmemClientPeer *
ivshmem_client_search_peer(IvshmemClient *client, int64_t peer_id);

void ivshmem_client_dump(const IvshmemClient *client);

#endif
