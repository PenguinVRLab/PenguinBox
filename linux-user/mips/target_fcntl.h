/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MIPS_TARGET_FCNTL_H
#define MIPS_TARGET_FCNTL_H

#define TARGET_O_APPEND         0x0008
#define TARGET_O_DSYNC          0x0010
#define TARGET_O_NONBLOCK       0x0080
#define TARGET_O_CREAT          0x0100
#define TARGET_O_TRUNC          0x0200
#define TARGET_O_EXCL           0x0400
#define TARGET_O_NOCTTY         0x0800
#define TARGET_FASYNC           0x1000  /* fcntl, for BSD compatibility */
#define TARGET_O_LARGEFILE      0x2000
#define TARGET___O_SYNC         0x4000
#define TARGET_O_DIRECT         0x8000

#define TARGET_F_GETLK         14
#define TARGET_F_SETLK         6
#define TARGET_F_SETLKW        7

#define TARGET_F_SETOWN        24
#define TARGET_F_GETOWN        23

#if (TARGET_ABI_BITS == 32)

struct target_flock {
    short l_type;
    short l_whence;
    abi_long l_start;
    abi_long l_len;
    abi_long l_sysid;
    int l_pid;
    abi_long pad[4];
};

#define TARGET_HAVE_ARCH_STRUCT_FLOCK

#endif

#define TARGET_F_GETLK64       33
#define TARGET_F_SETLK64       34
#define TARGET_F_SETLKW64      35

#include "../generic/fcntl.h"
#endif
