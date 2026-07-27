/*
 * QEMU replay core API
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC_REPLAY_H
#define EXEC_REPLAY_H

#include "qapi/qapi-types-replay.h"

extern ReplayMode replay_mode;

void replay_configure(struct QemuOpts *opts);
void replay_start(void);
void replay_finish(void);
void replay_add_blocker(const char *feature);
const char *replay_get_filename(void);

bool replay_reverse_step(void);
bool replay_reverse_continue(void);
bool replay_running_debug(void);
void replay_breakpoint(void);
void replay_gdb_attached(void);

bool replay_exception(void);
bool replay_has_exception(void);
bool replay_interrupt(void);
bool replay_has_interrupt(void);

void replay_save_random(int ret, void *buf, size_t len);
int replay_read_random(void *buf, size_t len);

#endif
