/*
 * Postcopy migration for RAM
 *
 * Copyright 2013 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Dave Gilbert  <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_POSTCOPY_RAM_H
#define QEMU_POSTCOPY_RAM_H

#include "qapi/qapi-types-migration.h"

bool postcopy_ram_supported_by_host(MigrationIncomingState *mis,
                                    Error **errp);

int postcopy_ram_incoming_setup(MigrationIncomingState *mis);

int postcopy_ram_incoming_init(MigrationIncomingState *mis, Error **errp);

int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis);

int postcopy_ram_prepare_discard(MigrationIncomingState *mis);

void postcopy_discard_send_init(MigrationState *ms, const char *name);

void postcopy_discard_send_range(MigrationState *ms, unsigned long start,
                                 unsigned long length);

void postcopy_discard_send_finish(MigrationState *ms);

int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb);

int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                             RAMBlock *rb);

typedef enum {
    POSTCOPY_INCOMING_NONE = 0,
    POSTCOPY_INCOMING_ADVISE,
    POSTCOPY_INCOMING_DISCARD,
    POSTCOPY_INCOMING_LISTENING,
    POSTCOPY_INCOMING_RUNNING,
    POSTCOPY_INCOMING_END
} PostcopyState;

PostcopyState postcopy_state_get(void);
PostcopyState postcopy_state_set(PostcopyState new_state);

void postcopy_fault_thread_notify(MigrationIncomingState *mis);

void postcopy_infrastructure_init(void);

enum PostcopyNotifyReason {
    POSTCOPY_NOTIFY_PROBE = 0,
    POSTCOPY_NOTIFY_INBOUND_ADVISE,
    POSTCOPY_NOTIFY_INBOUND_LISTEN,
    POSTCOPY_NOTIFY_INBOUND_END,
};

struct PostcopyNotifyData {
    enum PostcopyNotifyReason reason;
};

void postcopy_add_notifier(NotifierWithReturn *nn);
void postcopy_remove_notifier(NotifierWithReturn *n);
int postcopy_notify(enum PostcopyNotifyReason reason, Error **errp);

void postcopy_thread_create(MigrationIncomingState *mis,
                            QemuThread *thread, const char *name,
                            void *(*fn)(void *), int joinable);

struct PostCopyFD;

typedef int (*pcfdhandler)(struct PostCopyFD *pcfd, void *ufd);
typedef int (*pcfdwake)(struct PostCopyFD *pcfd, RAMBlock *rb, uint64_t offset);

struct PostCopyFD {
    int fd;
    void *data;
    pcfdhandler handler;
    pcfdwake waker;
    const char *idstr;
};

void postcopy_register_shared_ufd(struct PostCopyFD *pcfd);
void postcopy_unregister_shared_ufd(struct PostCopyFD *pcfd);
int postcopy_notify_shared_wake(RAMBlock *rb, uint64_t offset);
int postcopy_wake_shared(struct PostCopyFD *pcfd, uint64_t client_addr,
                         RAMBlock *rb);
int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t offset);

enum PostcopyChannels {
    RAM_CHANNEL_PRECOPY = 0,
    RAM_CHANNEL_POSTCOPY = 1,
    RAM_CHANNEL_MAX,
};

void postcopy_preempt_new_channel(MigrationIncomingState *mis, QEMUFile *file);
void postcopy_preempt_setup(MigrationState *s);
int postcopy_preempt_establish_channel(MigrationState *s);
bool postcopy_is_paused(MigrationStatus status);
void mark_postcopy_blocktime_begin(uintptr_t addr, uint32_t ptid,
                                   RAMBlock *rb);

int postcopy_incoming_setup(MigrationIncomingState *mis, Error **errp);
int postcopy_incoming_cleanup(MigrationIncomingState *mis);

#endif
