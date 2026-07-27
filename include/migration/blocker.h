/*
 * QEMU migration blockers
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

#ifndef MIGRATION_BLOCKER_H
#define MIGRATION_BLOCKER_H

#include "qapi/qapi-types-migration.h"

int migrate_add_blocker(Error **reasonp, Error **errp);

int migrate_add_blocker_internal(Error **reasonp, Error **errp);

void migrate_del_blocker(Error **reasonp);

int migrate_add_blocker_normal(Error **reasonp, Error **errp);

int migrate_add_blocker_modes(Error **reasonp, unsigned modes, Error **errp);

#endif
