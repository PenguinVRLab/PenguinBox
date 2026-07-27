/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)errno.h     8.5 (Berkeley) 1/21/94
 */

#ifndef ERRNO_DEFS_H
#define ERRNO_DEFS_H

#define TARGET_EPERM            1
#define TARGET_ENOENT           2
#define TARGET_ESRCH            3
#define TARGET_EINTR            4
#define TARGET_EIO              5
#define TARGET_ENXIO            6
#define TARGET_E2BIG            7
#define TARGET_ENOEXEC          8
#define TARGET_EBADF            9
#define TARGET_ECHILD           10
#define TARGET_EDEADLK          11
#define TARGET_ENOMEM           12
#define TARGET_EACCES           13
#define TARGET_EFAULT           14
#define TARGET_ENOTBLK          15
#define TARGET_EBUSY            16
#define TARGET_EEXIST           17
#define TARGET_EXDEV            18
#define TARGET_ENODEV           19
#define TARGET_ENOTDIR          20
#define TARGET_EISDIR           21
#define TARGET_EINVAL           22
#define TARGET_ENFILE           23
#define TARGET_EMFILE           24
#define TARGET_ENOTTY           25
#define TARGET_ETXTBSY          26
#define TARGET_EFBIG            27
#define TARGET_ENOSPC           28
#define TARGET_ESPIPE           29
#define TARGET_EROFS            30
#define TARGET_EMLINK           31
#define TARGET_EPIPE            32

#define TARGET_EDOM             33
#define TARGET_ERANGE           34

#define TARGET_EAGAIN           35
#define TARGET_EWOULDBLOCK      EAGAIN
#define TARGET_EINPROGRESS      36
#define TARGET_EALREADY 37

#define TARGET_ENOTSOCK 38
#define TARGET_EDESTADDRREQ     39
#define TARGET_EMSGSIZE 40
#define TARGET_EPROTOTYPE       41
#define TARGET_ENOPROTOOPT      42
#define TARGET_EPROTONOSUPPORT  43
#define TARGET_ESOCKTNOSUPPORT  44
#define TARGET_EOPNOTSUPP       45
#define TARGET_EPFNOSUPPORT     46
#define TARGET_EAFNOSUPPORT     47
#define TARGET_EADDRINUSE       48
#define TARGET_EADDRNOTAVAIL    49

#define TARGET_ENETDOWN 50
#define TARGET_ENETUNREACH      51
#define TARGET_ENETRESET        52
#define TARGET_ECONNABORTED     53
#define TARGET_ECONNRESET       54
#define TARGET_ENOBUFS          55
#define TARGET_EISCONN          56
#define TARGET_ENOTCONN 57
#define TARGET_ESHUTDOWN        58
#define TARGET_ETOOMANYREFS     59
#define TARGET_ETIMEDOUT        60
#define TARGET_ECONNREFUSED     61

#define TARGET_ELOOP            62
#define TARGET_ENAMETOOLONG     63

#define TARGET_EHOSTDOWN        64
#define TARGET_EHOSTUNREACH     65
#define TARGET_ENOTEMPTY        66

#define TARGET_EPROCLIM 67
#define TARGET_EUSERS           68
#define TARGET_EDQUOT           69

#define TARGET_ESTALE           70
#define TARGET_EREMOTE          71
#define TARGET_EBADRPC          72
#define TARGET_ERPCMISMATCH     73
#define TARGET_EPROGUNAVAIL     74
#define TARGET_EPROGMISMATCH    75
#define TARGET_EPROCUNAVAIL     76

#define TARGET_ENOLCK           77
#define TARGET_ENOSYS           78

#define TARGET_EFTYPE           79
#define TARGET_EAUTH            80
#define TARGET_ENEEDAUTH        81
#define TARGET_EIPSEC           82
#define TARGET_ENOATTR          83
#define TARGET_EILSEQ           84
#define TARGET_ENOMEDIUM        85
#define TARGET_EMEDIUMTYPE      86
#define TARGET_EOVERFLOW        87
#define TARGET_ECANCELED        88
#define TARGET_EIDRM            89
#define TARGET_ENOMSG           90
#define TARGET_ELAST            90

#define TARGET_EJUSTRETURN      254
#define TARGET_ERESTART         255

#include "special-errno.h"

_Static_assert(TARGET_ERESTART == QEMU_ERESTARTSYS,
               "TARGET_ERESTART and QEMU_ERESTARTSYS expected to match");

#endif
