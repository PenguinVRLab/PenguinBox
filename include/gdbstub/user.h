/*
 * gdbstub user-mode only APIs
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef GDBSTUB_USER_H
#define GDBSTUB_USER_H

#define MAX_SIGINFO_LENGTH 128

int gdb_handlesig(CPUState *, int, const char *, void *, int);

void gdb_signalled(CPUArchState *as, int sig);

void gdbserver_fork_start(void);

void gdbserver_fork_end(CPUState *cs, pid_t pid);

void gdb_syscall_entry(CPUState *cs, int num);

void gdb_syscall_return(CPUState *cs, int num);

#endif
