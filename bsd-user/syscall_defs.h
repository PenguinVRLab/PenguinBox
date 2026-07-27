/*
 *  System call related declarations
 *
 *  Copyright (c) 2013-15 Stacey D. Son (sson at FreeBSD)
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

#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include <sys/syscall.h>
#include <sys/resource.h>

#include "errno_defs.h"

#include "freebsd/syscall_nr.h"
#include "netbsd/syscall_nr.h"
#include "openbsd/syscall_nr.h"

/*
 * time_t seems to be very inconsistly defined for the different *BSD's...
 *
 * FreeBSD uses a 64bits time_t except on i386
 * so we have to add a special case here.
 *
 * On NetBSD time_t is always defined as an int64_t.  On OpenBSD time_t
 * is always defined as an int.
 *
 */
#if (!defined(TARGET_I386))
typedef int64_t target_time_t;
#else
typedef int32_t target_time_t;
#endif

struct target_iovec {
    abi_long iov_base;
    abi_long iov_len;
};

struct target_ipc_perm {
    uint32_t    cuid;
    uint32_t    cgid;
    uint32_t    uid;
    uint32_t    gid;
    uint16_t    mode;
    uint16_t    seq;
    abi_long    key;
};

#define TARGET_IPC_RMID 0
#define TARGET_IPC_SET  1
#define TARGET_IPC_STAT 2

struct target_shmid_ds {
    struct  target_ipc_perm shm_perm;
    abi_ulong   shm_segsz;
    int32_t     shm_lpid;
    int32_t     shm_cpid;
    int32_t     shm_nattch;
    target_time_t shm_atime;
    target_time_t shm_dtime;
    target_time_t shm_ctime;
};

#define N_BSD_SHM_REGIONS   32
struct bsd_shm_regions {
    abi_long start;
    abi_long size;
};

#define TARGET_MADV_DONTNEED            4

#define TARGET_FREEBSD_MAP_RESERVED0080 0x0080
#define TARGET_FREEBSD_MAP_RESERVED0100 0x0100
#define TARGET_FREEBSD_MAP_STACK        0x0400
#define TARGET_FREEBSD_MAP_NOSYNC       0x0800

#define TARGET_FREEBSD_MAP_FLAGMASK     0x1ff7

#define TARGET_NETBSD_MAP_INHERIT       0x0080
#define TARGET_NETBSD_MAP_TRYFIXED      0x0400
#define TARGET_NETBSD_MAP_WIRED         0x0800

#define TARGET_NETBSD_MAP_STACK         0x2000

#define TARGET_NETBSD_MAP_FLAGMASK      0x3ff7

#define TARGET_OPENBSD_MAP_INHERIT      0x0080
#define TARGET_OPENBSD_MAP_NOEXTEND     0x0100
#define TARGET_OPENBSD_MAP_TRYFIXED     0x0400

#define TARGET_OPENBSD_MAP_FLAGMASK     0x17f7

#define TARGET_BSD_MAP_FLAGMASK         0x3ff7

typedef abi_long target_freebsd_suseconds_t;

struct target_freebsd_timespec {
    target_time_t   tv_sec;
    abi_long                tv_nsec;
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

#define TARGET_CPUCLOCK_WHICH_PID   0
#define TARGET_CPUCLOCK_WHICH_TID   1

struct target_freebsd__umtx_time {
    struct target_freebsd_timespec  _timeout;
    uint32_t    _flags;
    uint32_t    _clockid;
};

struct target_freebsd_timeval {
    target_time_t       tv_sec;
    target_freebsd_suseconds_t  tv_usec;
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

#define TARGET_RLIM_INFINITY    RLIM_INFINITY

#define TARGET_RLIMIT_CPU       0
#define TARGET_RLIMIT_FSIZE     1
#define TARGET_RLIMIT_DATA      2
#define TARGET_RLIMIT_STACK     3
#define TARGET_RLIMIT_CORE      4
#define TARGET_RLIMIT_RSS       5
#define TARGET_RLIMIT_MEMLOCK   6
#define TARGET_RLIMIT_NPROC     7
#define TARGET_RLIMIT_NOFILE    8
#define TARGET_RLIMIT_SBSIZE    9
#define TARGET_RLIMIT_AS        10
#define TARGET_RLIMIT_NPTS      11
#define TARGET_RLIMIT_SWAP      12

struct target_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

struct target_freebsd_rusage {
    struct target_freebsd_timeval ru_utime;
    struct target_freebsd_timeval ru_stime;
    abi_long    ru_maxrss;
    abi_long    ru_ixrss;
    abi_long    ru_idrss;
    abi_long    ru_isrss;
    abi_long    ru_minflt;
    abi_long    ru_majflt;
    abi_long    ru_nswap;
    abi_long    ru_inblock;
    abi_long    ru_oublock;
    abi_long    ru_msgsnd;
    abi_long    ru_msgrcv;
    abi_long    ru_nsignals;
    abi_long    ru_nvcsw;
    abi_long    ru_nivcsw;
};

struct target_freebsd__wrusage {
    struct target_freebsd_rusage wru_self;
    struct target_freebsd_rusage wru_children;
};

struct target_freebsd11_stat {
    uint32_t  st_dev;
    uint32_t  st_ino;
    int16_t   st_mode;
    int16_t   st_nlink;
    uint32_t  st_uid;
    uint32_t  st_gid;
    uint32_t  st_rdev;
    struct  target_freebsd_timespec st_atim;
    struct  target_freebsd_timespec st_mtim;
    struct  target_freebsd_timespec st_ctim;
    int64_t    st_size;
    int64_t    st_blocks;
    uint32_t   st_blksize;
    uint32_t   st_flags;
    uint32_t   st_gen;
    int32_t    st_lspare;
    struct target_freebsd_timespec st_birthtim;
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
} __packed;

#if defined(__i386__)
#define TARGET_HAS_STAT_TIME_T_EXT       1
#endif

struct target_stat {
    uint64_t  st_dev;
    uint64_t  st_ino;
    uint64_t  st_nlink;
    int16_t   st_mode;
    int16_t   st_padding0;
    uint32_t  st_uid;
    uint32_t  st_gid;
    int32_t   st_padding1;
    uint64_t  st_rdev;
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t   st_atim_ext;
#endif
    struct  target_freebsd_timespec st_atim;
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t   st_mtim_ext;
#endif
    struct  target_freebsd_timespec st_mtim;
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t st_ctim_ext;
#endif
    struct  target_freebsd_timespec st_ctim;
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t st_btim_ext;
#endif
    struct  target_freebsd_timespec st_birthtim;
    int64_t   st_size;
    int64_t   st_blocks;
    uint32_t  st_blksize;
    uint32_t  st_flags;
    uint64_t  st_gen;
    uint64_t  st_spare[10];
};


struct target_freebsd11_nstat {
    uint32_t  st_dev;
    uint32_t  st_ino;
    int16_t   st_mode;
    int16_t   st_nlink;
    uint32_t  st_uid;
    uint32_t  st_gid;
    uint32_t  st_rdev;
    struct  target_freebsd_timespec st_atim;
    struct  target_freebsd_timespec st_mtim;
    struct  target_freebsd_timespec st_ctim;
    int64_t    st_size;
    int64_t    st_blocks;
    uint32_t   st_blksize;
    uint32_t   st_flags;
    uint32_t   st_gen;
    struct target_freebsd_timespec st_birthtim;
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
} __packed;

typedef struct target_freebsd_fsid { int32_t val[2]; } target_freebsd_fsid_t;

struct target_freebsd11_statfs {
    uint32_t f_version;
    uint32_t f_type;
    uint64_t f_flags;
    uint64_t f_bsize;
    uint64_t f_iosize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    int64_t  f_bavail;
    uint64_t f_files;
    int64_t  f_ffree;
    uint64_t f_syncwrites;
    uint64_t f_asyncwrites;
    uint64_t f_syncreads;
    uint64_t f_asyncreads;
    uint64_t f_spare[10];
    uint32_t f_namemax;
    uint32_t f_owner;
    target_freebsd_fsid_t   f_fsid;
    char     f_charspare[80];
    char     f_fstypename[16];
    char     f_mntfromname[88];
    char     f_mntonname[88];
};

struct target_statfs {
    uint32_t f_version;
    uint32_t f_type;
    uint64_t f_flags;
    uint64_t f_bsize;
    uint64_t f_iosize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    int64_t  f_bavail;
    uint64_t f_files;
    int64_t  f_ffree;
    uint64_t f_syncwrites;
    uint64_t f_asyncwrites;
    uint64_t f_syncreads;
    uint64_t f_asyncreads;
    uint64_t f_spare[10];
    uint32_t f_namemax;
    uint32_t f_owner;
    target_freebsd_fsid_t f_fsid;
    char      f_charspare[80];
    char      f_fstypename[16];
    char      f_mntfromname[1024];
    char      f_mntonname[1024];
};

#define TARGET_MAXFIDSZ     16

struct target_freebsd_fid {
    uint16_t    fid_len;
    uint16_t    fid_data0;
    char        fid_data[TARGET_MAXFIDSZ];
};

struct target_freebsd_fhandle {
    target_freebsd_fsid_t   fh_fsid;
    struct target_freebsd_fid fh_fid;
};
typedef struct target_freebsd_fhandle target_freebsd_fhandle_t;

#define TARGET_F_DUPFD              0
#define TARGET_F_GETFD              1
#define TARGET_F_SETFD              2
#define TARGET_F_GETFL              3
#define TARGET_F_SETFL              4
#define TARGET_F_GETOWN             5
#define TARGET_F_SETOWN             6
#define TARGET_F_OGETLK             7
#define TARGET_F_OSETLK             8
#define TARGET_F_OSETLKW            9
#define TARGET_F_DUP2FD             10
#define TARGET_F_GETLK              11
#define TARGET_F_SETLK              12
#define TARGET_F_SETLKW             13
#define TARGET_F_SETLK_REMOTE       14
#define TARGET_F_READAHEAD          15
#define TARGET_F_RDAHEAD            16
#define TARGET_F_DUPFD_CLOEXEC     17
#define TARGET_F_DUP2FD_CLOEXEC    18
#define TARGET_F_ADD_SEALS          19
#define TARGET_F_GET_SEALS          20

struct target_freebsd_flock {
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int16_t l_type;
    int16_t l_whence;
    int32_t l_sysid;
} QEMU_PACKED;

#define TARGET_RFSPAWN (1U << 31)

#define TARGET_PROC_SPROTECT            1
#define TARGET_PROC_REAP_ACQUIRE        2
#define TARGET_PROC_REAP_RELEASE        3
#define TARGET_PROC_REAP_STATUS         4
#define TARGET_PROC_REAP_GETPIDS        5
#define TARGET_PROC_REAP_KILL           6

struct target_procctl_reaper_status {
    uint32_t rs_flags;
    uint32_t rs_children;
    uint32_t rs_descendants;
    uint32_t rs_reaper;
    uint32_t rs_pid;
    uint32_t rs_pad0[15];
};

struct target_procctl_reaper_pidinfo {
    uint32_t pi_pid;
    uint32_t pi_subtree;
    uint32_t pi_flags;
    uint32_t pi_pad0[15];
};

struct target_procctl_reaper_pids {
    uint32_t rp_count;
    uint32_t rp_pad0[15];
    abi_ulong rp_pids;
};

struct target_procctl_reaper_kill {
    int32_t  rk_sig;
    uint32_t rk_flags;
    uint32_t rk_subtree;
    uint32_t rk_killed;
    uint32_t rk_fpid;
    uint32_t rk_pad0[15];
};


#define safe_syscall0(type, name) \
type safe_##name(void) \
{ \
    return safe_syscall(SYS_##name); \
}

#define safe_syscall1(type, name, type1, arg1) \
type safe_##name(type1 arg1) \
{ \
    return safe_syscall(SYS_##name, arg1); \
}

#define safe_syscall2(type, name, type1, arg1, type2, arg2) \
type safe_##name(type1 arg1, type2 arg2) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2); \
}

#define safe_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3); \
}

#define safe_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4); \
}

#define safe_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4, arg5); \
}

#define safe_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5, type6, arg6) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5, type6 arg6) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4, arg5, arg6); \
}

#define safe_fcntl(...) safe_syscall(SYS_fcntl, __VA_ARGS__)

#undef  target_to_host_bitmask
#define target_to_host_bitmask(x, tbl) (x)
#undef  host_to_target_bitmask
#define host_to_target_bitmask(x, tbl) (x)

#endif
