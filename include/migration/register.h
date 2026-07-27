/*
 * QEMU migration vmstate registration
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

#ifndef MIGRATION_REGISTER_H
#define MIGRATION_REGISTER_H

#include "hw/vmstate-if.h"

typedef struct SaveVMHandlers {

    void (*save_state)(QEMUFile *f, void *opaque);

    int (*save_prepare)(void *opaque, Error **errp);

    int (*save_setup)(QEMUFile *f, void *opaque, Error **errp);

    void (*save_cleanup)(void *opaque);

    int (*save_complete)(QEMUFile *f, void *opaque);

    SaveCompletePrecopyThreadHandler save_complete_precopy_thread;

    bool (*is_active)(void *opaque);

    bool (*has_postcopy)(void *opaque);

    bool (*is_active_iterate)(void *opaque);

    int (*save_live_iterate)(QEMUFile *f, void *opaque);

    bool (*save_postcopy_prepare)(QEMUFile *f, void *opaque, Error **errp);

    void (*state_pending_estimate)(void *opaque, uint64_t *must_precopy,
                                   uint64_t *can_postcopy);

    void (*state_pending_exact)(void *opaque, uint64_t *must_precopy,
                                uint64_t *can_postcopy);

    int (*load_state)(QEMUFile *f, void *opaque, int version_id);

    bool (*load_state_buffer)(void *opaque, char *buf, size_t len,
                              Error **errp);

    int (*load_setup)(QEMUFile *f, void *opaque, Error **errp);

    int (*load_cleanup)(void *opaque);

    int (*resume_prepare)(MigrationState *s, void *opaque);

    bool (*switchover_ack_needed)(void *opaque);

    int (*switchover_start)(void *opaque);
} SaveVMHandlers;

int register_savevm_live(const char *idstr,
                         uint32_t instance_id,
                         int version_id,
                         const SaveVMHandlers *ops,
                         void *opaque);

void unregister_savevm(VMStateIf *obj, const char *idstr, void *opaque);

#endif
