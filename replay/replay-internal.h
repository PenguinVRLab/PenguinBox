#ifndef REPLAY_INTERNAL_H
#define REPLAY_INTERNAL_H

/*
 * replay-internal.h
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

typedef enum ReplayAsyncEventKind {
    REPLAY_ASYNC_EVENT_BH,
    REPLAY_ASYNC_EVENT_BH_ONESHOT,
    REPLAY_ASYNC_EVENT_INPUT,
    REPLAY_ASYNC_EVENT_INPUT_SYNC,
    REPLAY_ASYNC_EVENT_CHAR_READ,
    REPLAY_ASYNC_EVENT_BLOCK,
    REPLAY_ASYNC_EVENT_NET,
    REPLAY_ASYNC_COUNT
} ReplayAsyncEventKind;

enum ReplayEvents {
    EVENT_INSTRUCTION,
    EVENT_INTERRUPT,
    EVENT_EXCEPTION,
    EVENT_ASYNC,
    EVENT_ASYNC_LAST = EVENT_ASYNC + REPLAY_ASYNC_COUNT - 1,
    EVENT_SHUTDOWN,
    EVENT_SHUTDOWN_LAST = EVENT_SHUTDOWN + SHUTDOWN_CAUSE__MAX,
    EVENT_CHAR_WRITE,
    EVENT_CHAR_READ_ALL,
    EVENT_CHAR_READ_ALL_ERROR,
    EVENT_AUDIO_OUT,
    EVENT_AUDIO_IN,
    EVENT_RANDOM,
    EVENT_CLOCK,
    EVENT_CLOCK_LAST = EVENT_CLOCK + REPLAY_CLOCK_COUNT - 1,
    EVENT_CHECKPOINT,
    EVENT_CHECKPOINT_LAST = EVENT_CHECKPOINT + CHECKPOINT_COUNT - 1,
    EVENT_END,
    EVENT_COUNT
};

typedef struct ReplayState {
    int64_t cached_clock[REPLAY_CLOCK_COUNT];
    uint64_t current_icount;
    int instruction_count;
    unsigned int current_event;
    unsigned int data_kind;
    bool has_unread_data;
    uint64_t file_offset;
    uint64_t block_request_id;
    uint64_t read_event_id;
} ReplayState;
extern ReplayState replay_state;

extern FILE *replay_file;
extern uint64_t replay_break_icount;
extern QEMUTimer *replay_break_timer;

void replay_put_byte(uint8_t byte);
void replay_put_event(uint8_t event);
void replay_put_word(uint16_t word);
void replay_put_dword(uint32_t dword);
void replay_put_qword(int64_t qword);
void replay_put_array(const uint8_t *buf, size_t size);

uint8_t replay_get_byte(void);
uint16_t replay_get_word(void);
uint32_t replay_get_dword(void);
int64_t replay_get_qword(void);
void replay_get_array(uint8_t *buf, size_t *size);
void replay_get_array_alloc(uint8_t **buf, size_t *size);

void replay_mutex_init(void);
bool replay_mutex_locked(void);

void replay_check_error(void);

void replay_finish_event(void);
void replay_fetch_data_kind(void);

void replay_advance_current_icount(uint64_t current_icount);
void replay_save_instructions(void);

bool replay_next_event_is(int event);

void replay_read_next_clock(ReplayClockKind kind);

void replay_init_events(void);
void replay_finish_events(void);
bool replay_has_events(void);
void replay_save_events(void);
void replay_read_events(void);
void replay_add_event(ReplayAsyncEventKind event_kind, void *opaque,
                      void *opaque2, uint64_t id);

void replay_save_input_event(InputEvent *evt);
InputEvent *replay_read_input_event(void);
void replay_add_input_event(struct InputEvent *event);
void replay_add_input_sync_event(void);

void replay_event_char_read_run(void *opaque);
void replay_event_char_read_save(void *opaque);
void *replay_event_char_read_load(void);

void replay_event_net_run(void *opaque);
void replay_event_net_save(void *opaque);
void *replay_event_net_load(void);

G_NORETURN void replay_sync_error(const char *error);

void replay_vmstate_register(void);

#endif
