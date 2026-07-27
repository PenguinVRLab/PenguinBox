/*
 * QEMU replay (system interface)
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef SYSTEM_REPLAY_H
#define SYSTEM_REPLAY_H

#include "exec/replay-core.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qapi-types-run-state.h"
#include "qapi/qapi-types-ui.h"
#include "block/aio.h"
#include "qemu/audio.h"

enum ReplayClockKind {
    REPLAY_CLOCK_HOST,
    REPLAY_CLOCK_VIRTUAL_RT,
    REPLAY_CLOCK_COUNT
};
typedef enum ReplayClockKind ReplayClockKind;

enum ReplayCheckpoint {
    CHECKPOINT_CLOCK_WARP_START,
    CHECKPOINT_CLOCK_WARP_ACCOUNT,
    CHECKPOINT_RESET_REQUESTED,
    CHECKPOINT_SUSPEND_REQUESTED,
    CHECKPOINT_CLOCK_VIRTUAL,
    CHECKPOINT_CLOCK_HOST,
    CHECKPOINT_CLOCK_VIRTUAL_RT,
    CHECKPOINT_INIT,
    CHECKPOINT_RESET,
    CHECKPOINT_COUNT
};
typedef enum ReplayCheckpoint ReplayCheckpoint;

typedef struct ReplayNetState ReplayNetState;

extern char *replay_snapshot;

void replay_mutex_lock(void);
void replay_mutex_unlock(void);

uint64_t replay_get_current_icount(void);
int replay_get_instructions(void);
void replay_account_executed_instructions(void);

int64_t replay_save_clock(ReplayClockKind kind, int64_t clock,
                          int64_t raw_icount);
int64_t replay_read_clock(ReplayClockKind kind, int64_t raw_icount);
#define REPLAY_CLOCK(clock, value)                                      \
    !icount_enabled() ? (value) :                                       \
    (replay_mode == REPLAY_MODE_PLAY                                    \
        ? replay_read_clock((clock), icount_get_raw())                  \
        : replay_mode == REPLAY_MODE_RECORD                             \
            ? replay_save_clock((clock), (value), icount_get_raw())     \
            : (value))
#define REPLAY_CLOCK_LOCKED(clock, value)                               \
    !icount_enabled() ? (value) :                                       \
    (replay_mode == REPLAY_MODE_PLAY                                    \
        ? replay_read_clock((clock), icount_get_raw_locked())           \
        : replay_mode == REPLAY_MODE_RECORD                             \
            ? replay_save_clock((clock), (value), icount_get_raw_locked()) \
            : (value))

void replay_shutdown_request(ShutdownCause cause);
bool replay_checkpoint(ReplayCheckpoint checkpoint);
bool replay_has_event(void);
void replay_async_events(void);

void replay_enable_events(void);
bool replay_events_enabled(void);
void replay_flush_events(void);
void replay_bh_schedule_event(QEMUBH *bh);
void replay_bh_schedule_oneshot_event(AioContext *ctx,
    QEMUBHFunc *cb, void *opaque);
void replay_input_event(QemuConsole *src, InputEvent *evt);
void replay_input_sync_event(void);
void replay_block_event(QEMUBH *bh, uint64_t id);
uint64_t blkreplay_next_id(void);

void replay_register_char_driver(struct Chardev *chr);
void replay_chr_be_write(struct Chardev *s, const uint8_t *buf, int len);
void replay_char_write_event_save(int res, int offset);
void replay_char_write_event_load(int *res, int *offset);
int replay_char_read_all_load(uint8_t *buf);
void replay_char_read_all_save_error(int res);
void replay_char_read_all_save_buf(uint8_t *buf, int offset);

ReplayNetState *replay_register_net(NetFilterState *nfs);
void replay_unregister_net(ReplayNetState *rns);
void replay_net_packet_event(ReplayNetState *rns, unsigned flags,
                             const struct iovec *iov, int iovcnt);

void replay_audio_out(size_t *played);
void replay_audio_in(size_t *recorded, st_sample *samples, size_t *wpos, size_t size);

void replay_vmstate_init(void);
bool replay_can_snapshot(void);

#endif
