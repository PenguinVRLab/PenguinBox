/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef ALPHA_TARGET_FCNTL_H
#define ALPHA_TARGET_FCNTL_H

#define TARGET_O_NONBLOCK           04
#define TARGET_O_APPEND            010
#define TARGET_O_CREAT           01000
#define TARGET_O_TRUNC           02000
#define TARGET_O_EXCL            04000
#define TARGET_O_NOCTTY         010000
#define TARGET_O_DSYNC          040000
#define TARGET_O_LARGEFILE           0
#define TARGET_O_DIRECTORY     0100000
#define TARGET_O_NOFOLLOW      0200000
#define TARGET_O_DIRECT       02000000
#define TARGET_O_NOATIME      04000000
#define TARGET_O_CLOEXEC     010000000
#define TARGET___O_SYNC      020000000
#define TARGET_O_PATH        040000000
#define TARGET___O_TMPFILE  0100000000

#define TARGET_F_GETLK         7
#define TARGET_F_SETLK         8
#define TARGET_F_SETLKW        9
#define TARGET_F_SETOWN        5
#define TARGET_F_GETOWN        6

#define TARGET_F_RDLCK         1
#define TARGET_F_WRLCK         2
#define TARGET_F_UNLCK         8

#include "../generic/fcntl.h"
#endif
