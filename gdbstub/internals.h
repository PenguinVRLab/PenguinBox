/*
 * gdbstub internals
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GDBSTUB_INTERNALS_H
#define GDBSTUB_INTERNALS_H

#include "exec/cpu-common.h"

#define MAX_PACKET_LENGTH 131104

enum {
    GDB_SIGNAL_0 = 0,
    GDB_SIGNAL_INT = 2,
    GDB_SIGNAL_QUIT = 3,
    GDB_SIGNAL_TRAP = 5,
    GDB_SIGNAL_ABRT = 6,
    GDB_SIGNAL_ALRM = 14,
    GDB_SIGNAL_STOP = 17,
    GDB_SIGNAL_IO = 23,
    GDB_SIGNAL_XCPU = 24,
    GDB_SIGNAL_UNKNOWN = 143
};

typedef struct GDBProcess {
    uint32_t pid;
    bool attached;
    char *target_xml;
} GDBProcess;

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_GETLINE_ESC,
    RS_GETLINE_RLE,
    RS_CHKSUM1,
    RS_CHKSUM2,
};

typedef struct GDBState {
    bool init;
    CPUState *c_cpu;
    CPUState *g_cpu;
    CPUState *query_cpu;
    enum RSState state;
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum;
    int line_csum;
    GByteArray *last_packet;
    int signal;
    bool multiprocess;
    GDBProcess *processes;
    int process_num;
    GString *str_buf;
    GByteArray *mem_buf;
    int sstep_flags;
    int supported_sstep_flags;
    bool allow_stop_reply;
} GDBState;

extern GDBState gdbserver_state;

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9') {
        return v - '0';
    } else if (v >= 'A' && v <= 'F') {
        return v - 'A' + 10;
    } else if (v >= 'a' && v <= 'f') {
        return v - 'a' + 10;
    } else {
        return 0;
    }
}

static inline int tohex(int v)
{
    if (v < 10) {
        return v + '0';
    } else {
        return v - 10 + 'a';
    }
}

void gdb_put_strbuf(void);
int gdb_put_packet_binary(const char *buf, int len, bool dump);
void gdb_memtohex(GString *buf, const uint8_t *mem, int len);
void gdb_memtox(GString *buf, const char *mem, int len);
void gdb_read_byte(uint8_t ch);

bool gdb_got_immediate_ack(void);
GDBProcess *gdb_get_process(uint32_t pid);
CPUState *gdb_get_first_cpu_in_process(GDBProcess *process);
CPUState *gdb_first_attached_cpu(void);
void gdb_append_thread_id(CPUState *cpu, GString *buf);
int gdb_get_cpu_index(CPUState *cpu);
unsigned int gdb_get_max_cpus(void);
bool gdb_can_reverse(void);
int gdb_target_sigtrap(void);

void gdb_create_default_process(GDBState *s);

int gdb_signal_to_target(int sig);
int gdb_target_signal_to_gdb(int sig);

int gdb_get_char(void);

void gdb_continue(void);

int gdb_continue_partial(char *newstates);

void gdb_put_buffer(const uint8_t *buf, int len);

void gdb_init_gdbserver_state(void);

void gdb_handle_query_rcmd(GArray *params, void *ctx);
void gdb_handle_query_offsets(GArray *params, void *user_ctx);
void gdb_handle_query_xfer_auxv(GArray *params, void *user_ctx);
void gdb_handle_query_xfer_siginfo(GArray *params, void *user_ctx);
void gdb_handle_v_file_open(GArray *params, void *user_ctx);
void gdb_handle_v_file_close(GArray *params, void *user_ctx);
void gdb_handle_v_file_pread(GArray *params, void *user_ctx);
void gdb_handle_v_file_readlink(GArray *params, void *user_ctx);
void gdb_handle_query_xfer_exec_file(GArray *params, void *user_ctx);
void gdb_handle_set_catch_syscalls(GArray *params, void *user_ctx);
void gdb_handle_query_supported_user(const char *gdb_supported);
bool gdb_handle_set_thread_user(uint32_t pid, uint32_t tid);
bool gdb_handle_detach_user(uint32_t pid);

void gdb_handle_query_attached(GArray *params, void *ctx);

void gdb_handle_query_qemu_phy_mem_mode(GArray *params, void *ctx);
void gdb_handle_set_qemu_phy_mem_mode(GArray *params, void *ctx);

void gdb_handle_file_io(GArray *params, void *user_ctx);
bool gdb_handled_syscall(void);
void gdb_disable_syscalls(void);
void gdb_syscall_reset(void);

void gdb_syscall_handling(const char *syscall_packet);

bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, vaddr addr, vaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, vaddr addr, vaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

int gdb_target_memory_rw_debug(CPUState *cs, hwaddr addr,
                               uint8_t *buf, int len, bool is_write);

#endif
