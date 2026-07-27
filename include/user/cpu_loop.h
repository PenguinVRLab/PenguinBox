/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef USER_CPU_LOOP_H
#define USER_CPU_LOOP_H

#include "exec/vaddr.h"
#include "exec/mmu-access-type.h"


MMUAccessType adjust_signal_pc(uintptr_t *pc, bool is_write);

bool handle_sigsegv_accerr_write(CPUState *cpu, sigset_t *old_set,
                                 uintptr_t host_pc, vaddr guest_addr);

G_NORETURN void cpu_loop_exit_sigsegv(CPUState *cpu, vaddr addr,
                                      MMUAccessType access_type,
                                      bool maperr, uintptr_t ra);

G_NORETURN void cpu_loop_exit_sigbus(CPUState *cpu, vaddr addr,
                                     MMUAccessType access_type,
                                     uintptr_t ra);

G_NORETURN void cpu_loop(CPUArchState *env);

void target_exception_dump(CPUArchState *env, const char *fmt, int code);
#define EXCP_DUMP(env, fmt, code) \
    target_exception_dump(env, fmt, code)

#endif
