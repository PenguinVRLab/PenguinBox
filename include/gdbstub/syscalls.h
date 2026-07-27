/*
 * GDB Syscall support
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

#define GDB_O_RDONLY  0
#define GDB_O_WRONLY  1
#define GDB_O_RDWR    2
#define GDB_O_APPEND  8
#define GDB_O_CREAT   0x200
#define GDB_O_TRUNC   0x400
#define GDB_O_EXCL    0x800

#define GDB_EPERM           1
#define GDB_ENOENT          2
#define GDB_EINTR           4
#define GDB_EBADF           9
#define GDB_EACCES         13
#define GDB_EFAULT         14
#define GDB_EBUSY          16
#define GDB_EEXIST         17
#define GDB_ENODEV         19
#define GDB_ENOTDIR        20
#define GDB_EISDIR         21
#define GDB_EINVAL         22
#define GDB_ENFILE         23
#define GDB_EMFILE         24
#define GDB_EFBIG          27
#define GDB_ENOSPC         28
#define GDB_ESPIPE         29
#define GDB_EROFS          30
#define GDB_ENAMETOOLONG   91
#define GDB_EUNKNOWN       9999

#define GDB_SEEK_SET  0
#define GDB_SEEK_CUR  1
#define GDB_SEEK_END  2

typedef uint32_t gdb_mode_t;
typedef uint32_t gdb_time_t;

struct gdb_stat {
  uint32_t    gdb_st_dev;
  uint32_t    gdb_st_ino;
  gdb_mode_t  gdb_st_mode;
  uint32_t    gdb_st_nlink;
  uint32_t    gdb_st_uid;
  uint32_t    gdb_st_gid;
  uint32_t    gdb_st_rdev;
  uint64_t    gdb_st_size;
  uint64_t    gdb_st_blksize;
  uint64_t    gdb_st_blocks;
  gdb_time_t  gdb_st_atime;
  gdb_time_t  gdb_st_mtime;
  gdb_time_t  gdb_st_ctime;
} QEMU_PACKED;

struct gdb_timeval {
  gdb_time_t tv_sec;
  uint64_t tv_usec;
} QEMU_PACKED;

typedef void (*gdb_syscall_complete_cb)(CPUState *cpu, uint64_t ret, int err);

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...);

int use_gdb_syscalls(void);

void gdb_exit(int code);

void gdb_qemu_exit(int code);

#endif
