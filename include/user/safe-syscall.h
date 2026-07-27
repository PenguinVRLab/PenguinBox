/*
 * safe-syscall.h: prototypes for linux-user signal-race-safe syscalls
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

#ifndef LINUX_USER_SAFE_SYSCALL_H
#define LINUX_USER_SAFE_SYSCALL_H

long safe_syscall_base(int *pending, long number, ...);
long safe_syscall_set_errno_tail(int value);

extern char safe_syscall_start[];
extern char safe_syscall_end[];

#define safe_syscall(...)                                                 \
    safe_syscall_base(&get_task_state(thread_cpu)->signal_pending,        \
                      __VA_ARGS__)

#endif
