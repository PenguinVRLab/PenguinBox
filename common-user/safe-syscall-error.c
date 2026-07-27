/*
 * safe-syscall-error.c: errno setting fragment
 * This is intended to be invoked by safe-syscall.S
 *
 * Written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "user/safe-syscall.h"

long safe_syscall_set_errno_tail(int value)
{
    errno = value;
    return -1;
}
