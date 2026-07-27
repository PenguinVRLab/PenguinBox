/*
 *  FreeBSD siginfo related definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef TARGET_OS_SIGINFO_H
#define TARGET_OS_SIGINFO_H

#define TARGET_NSIG         128
#define TARGET_NSIG_BPW     (sizeof(uint32_t) * 8)
#define TARGET_NSIG_WORDS   (TARGET_NSIG / TARGET_NSIG_BPW)

typedef struct target_sigaltstack {
    abi_long    ss_sp;
    abi_ulong   ss_size;
    abi_long    ss_flags;
} target_stack_t;

typedef struct {
    uint32_t __bits[TARGET_NSIG_WORDS];
} target_sigset_t;

struct target_sigaction {
    abi_ulong   _sa_handler;
    int32_t     sa_flags;
    target_sigset_t sa_mask;
};

typedef union target_sigval {
    int32_t sival_int;
    abi_ulong sival_ptr;
    int32_t sigval_int;
    abi_ulong sigval_ptr;
} target_sigval_t;

typedef struct target_siginfo {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t si_pid;
    int32_t si_uid;
    int32_t si_status;
    abi_ulong si_addr;
    union target_sigval si_value;
    union {
        struct {
            int32_t _trapno;
        } _fault;

        struct {
            int32_t _timerid;
            int32_t _overrun;
        } _timer;

        struct {
            int32_t _mqd;
        } _mesgp;

        struct {
            int _band;
        } _poll;

        struct {
            int _mqd;
        } _mesgq;

        struct {
            int _syscall;
        } _capsicum;

        struct {
            abi_long __spare1__;
            int32_t  __spare2_[7];
        } __spare__;
    } _reason;
} target_siginfo_t;

struct target_sigevent {
    abi_int sigev_notify;
    abi_int sigev_signo;
    target_sigval_t sigev_value;
    union {
        abi_int _threadid;

        struct {
            abi_ulong _function;
            abi_ulong _attribute;
        } _sigev_thread;
        abi_ushort _kevent_flags;
        abi_long _pad[8];
    } _sigev_un;
};

#define target_si_signo     si_signo
#define target_si_code      si_code
#define target_si_errno     si_errno
#define target_si_addr      si_addr

#define TARGET_ILL_ILLOPC   (1)
#define TARGET_ILL_ILLOPN   (2)
#define TARGET_ILL_ILLADR   (3)
#define TARGET_ILL_ILLTRP   (4)
#define TARGET_ILL_PRVOPC   (5)
#define TARGET_ILL_PRVREG   (6)
#define TARGET_ILL_COPROC   (7)
#define TARGET_ILL_BADSTK   (8)

#define TARGET_SEGV_MAPERR  (1)
#define TARGET_SEGV_ACCERR  (2)

#define TARGET_TRAP_BRKPT   (1)
#define TARGET_TRAP_TRACE   (2)

#define TARGET_BUS_ADRALN   (1)
#define TARGET_BUS_ADRERR   (2)
#define TARGET_BUS_OBJERR   (3)

#define TARGET_FPE_INTOVF   (1)
#define TARGET_FPE_INTDIV   (2)
#define TARGET_FPE_FLTDIV   (3)
#define TARGET_FPE_FLTOVF   (4)
#define TARGET_FPE_FLTUND   (5)
#define TARGET_FPE_FLTRES   (6)
#define TARGET_FPE_FLTINV   (7)
#define TARGET_FPE_FLTSUB   (8)

#endif
