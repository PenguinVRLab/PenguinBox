/*
 * Copyright 6WIND S.A., 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef IVSHMEM_SERVER_H
#define IVSHMEM_SERVER_H

#include <sys/select.h>

#include "qemu/event_notifier.h"
#include "qemu/queue.h"
#include "hw/misc/ivshmem.h"

#define IVSHMEM_SERVER_MAX_VECTORS 64

typedef struct IvshmemServerPeer {
    QTAILQ_ENTRY(IvshmemServerPeer) next;
    int sock_fd;
    int64_t id;
    EventNotifier vectors[IVSHMEM_SERVER_MAX_VECTORS];
    unsigned vectors_count;
} IvshmemServerPeer;

typedef struct IvshmemServer {
    char unix_sock_path[PATH_MAX];
    int sock_fd;
    char shm_path[PATH_MAX];
    bool use_shm_open;
    size_t shm_size;
    int shm_fd;
    unsigned n_vectors;
    uint16_t cur_id;
    bool verbose;
    QTAILQ_HEAD(, IvshmemServerPeer) peer_list;
} IvshmemServer;

int
ivshmem_server_init(IvshmemServer *server, const char *unix_sock_path,
                    const char *shm_path, bool use_shm_open,
                    size_t shm_size, unsigned n_vectors,
                    bool verbose);

int ivshmem_server_start(IvshmemServer *server);

void ivshmem_server_close(IvshmemServer *server);

void
ivshmem_server_get_fds(const IvshmemServer *server, fd_set *fds, int *maxfd);

int ivshmem_server_handle_fds(IvshmemServer *server, fd_set *fds, int maxfd);

IvshmemServerPeer *
ivshmem_server_search_peer(IvshmemServer *server, int64_t peer_id);

void ivshmem_server_dump(const IvshmemServer *server);

#endif
