/*
 *  BSD syscalls
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2013-2014 Stacey D. Son
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
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include <sys/syscall.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <utime.h>

#include "include/gdbstub/syscalls.h"

#include "qemu.h"
#include "signal-common.h"
#include "user/syscall-trace.h"

/* BSD independent syscall shims */
#include "bsd-file.h"
#include "bsd-mem.h"
#include "bsd-proc.h"

/* BSD dependent syscall shims */
#include "os-stat.h"
#include "os-proc.h"
#include "os-misc.h"

safe_syscall3(int, open, const char *, path, int, flags, mode_t, mode);
safe_syscall4(int, openat, int, fd, const char *, path, int, flags, mode_t,
    mode);

safe_syscall3(ssize_t, read, int, fd, void *, buf, size_t, nbytes);
safe_syscall4(ssize_t, pread, int, fd, void *, buf, size_t, nbytes, off_t,
    offset);
safe_syscall3(ssize_t, readv, int, fd, const struct iovec *, iov, int, iovcnt);
safe_syscall4(ssize_t, preadv, int, fd, const struct iovec *, iov, int, iovcnt,
    off_t, offset);

safe_syscall3(ssize_t, write, int, fd, void *, buf, size_t, nbytes);
safe_syscall4(ssize_t, pwrite, int, fd, void *, buf, size_t, nbytes, off_t,
    offset);
safe_syscall3(ssize_t, writev, int, fd, const struct iovec *, iov, int, iovcnt);
safe_syscall4(ssize_t, pwritev, int, fd, const struct iovec *, iov, int, iovcnt,
    off_t, offset);

safe_syscall4(pid_t, wait4, pid_t, wpid, int *, status, int, options,
    struct rusage *, rusage);
safe_syscall6(pid_t, wait6, idtype_t, idtype, id_t, id, int *, status, int,
    options, struct __wrusage *, wrusage, siginfo_t *, infop);

abi_long get_errno(abi_long ret)
{
    if (ret == -1) {
        return -host_to_target_errno(errno);
    } else {
        return ret;
    }
}

int host_to_target_errno(int err)
{
    /*
     * All the BSDs have the property that the error numbers are uniform across
     * all architectures for a given BSD, though they may vary between different
     * BSDs.
     */
    return err;
}

bool is_error(abi_long ret)
{
    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

static void helper_unlock_iovec(struct target_iovec *target_vec,
                                abi_ulong target_addr, struct iovec *vec,
                                int count, int copy)
{
    for (int i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);

        if (vec[i].iov_base) {
            unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
        }
    }
    unlock_user(target_vec, target_addr, 0);
}

struct iovec *lock_iovec(int type, abi_ulong target_addr,
        int count, int copy)
{
    struct target_iovec *target_vec;
    struct iovec *vec;
    abi_ulong total_len, max_len;
    int i;
    int err = 0;

    if (count == 0) {
        errno = 0;
        return NULL;
    }
    if (count < 0 || count > IOV_MAX) {
        errno = EINVAL;
        return NULL;
    }

    vec = g_try_new0(struct iovec, count);
    if (vec == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec == NULL) {
        err = EFAULT;
        goto fail2;
    }

    max_len = 0x7fffffff & MIN(TARGET_PAGE_MASK, PAGE_MASK);
    total_len = 0;

    for (i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);
        abi_long len = tswapal(target_vec[i].iov_len);

        if (len < 0) {
            err = EINVAL;
            goto fail;
        } else if (len == 0) {
            vec[i].iov_base = 0;
        } else {
            vec[i].iov_base = lock_user(type, base, len, copy);
            if (!vec[i].iov_base) {
                if (i == 0) {
                    err = EFAULT;
                    goto fail;
                } else {
                    goto out;
                }
            }
            if (len > max_len - total_len) {
                len = max_len - total_len;
            }
        }
        vec[i].iov_len = len;
        total_len += len;
    }
out:
    unlock_user(target_vec, target_addr, 0);
    return vec;

fail:
    helper_unlock_iovec(target_vec, target_addr, vec, i, copy);
fail2:
    g_free(vec);
    errno = err;
    return NULL;
}

void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
        int count, int copy)
{
    struct target_iovec *target_vec;

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec) {
        helper_unlock_iovec(target_vec, target_addr, vec, count, copy);
    }

    g_free(vec);
}

static abi_long freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                                abi_long arg2, abi_long arg3, abi_long arg4,
                                abi_long arg5, abi_long arg6, abi_long arg7,
                                abi_long arg8)
{
    abi_long ret;

    switch (num) {
    case TARGET_FREEBSD_NR_fork:
        ret = do_freebsd_fork(cpu_env);
        break;

    case TARGET_FREEBSD_NR_vfork:
        ret = do_freebsd_vfork(cpu_env);
        break;

    case TARGET_FREEBSD_NR_rfork:
        ret = do_freebsd_rfork(cpu_env, arg1);
        break;

    case TARGET_FREEBSD_NR_pdfork:
        ret = do_freebsd_pdfork(cpu_env, arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_execve:
        ret = do_freebsd_execve(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_fexecve:
        ret = do_freebsd_fexecve(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_wait4:
        ret = do_freebsd_wait4(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_wait6:
        ret = do_freebsd_wait6(cpu_env, arg1, arg2, arg3,
                               arg4, arg5, arg6, arg7, arg8);
        break;

    case TARGET_FREEBSD_NR_exit:
        ret = do_bsd_exit(cpu_env, arg1);
        break;

    case TARGET_FREEBSD_NR_getgroups:
        ret = do_bsd_getgroups(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setgroups:
        ret = do_bsd_setgroups(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_umask:
        ret = do_bsd_umask(arg1);
        break;

    case TARGET_FREEBSD_NR_setlogin:
        ret = do_bsd_setlogin(arg1);
        break;

    case TARGET_FREEBSD_NR_getlogin:
        ret = do_bsd_getlogin(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getrusage:
        ret = do_bsd_getrusage(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getrlimit:
        ret = do_bsd_getrlimit(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setrlimit:
        ret = do_bsd_setrlimit(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getpid:
        ret = do_bsd_getpid();
        break;

    case TARGET_FREEBSD_NR_getppid:
        ret = do_bsd_getppid();
        break;

    case TARGET_FREEBSD_NR_getuid:
        ret = do_bsd_getuid();
        break;

    case TARGET_FREEBSD_NR_geteuid:
        ret = do_bsd_geteuid();
        break;

    case TARGET_FREEBSD_NR_getgid:
        ret = do_bsd_getgid();
        break;

    case TARGET_FREEBSD_NR_getegid:
        ret = do_bsd_getegid();
        break;

    case TARGET_FREEBSD_NR_setuid:
        ret = do_bsd_setuid(arg1);
        break;

    case TARGET_FREEBSD_NR_seteuid:
        ret = do_bsd_seteuid(arg1);
        break;

    case TARGET_FREEBSD_NR_setgid:
        ret = do_bsd_setgid(arg1);
        break;

    case TARGET_FREEBSD_NR_setegid:
        ret = do_bsd_setegid(arg1);
        break;

    case TARGET_FREEBSD_NR_getpgrp:
        ret = do_bsd_getpgrp();
        break;

    case TARGET_FREEBSD_NR_getpgid:
         ret = do_bsd_getpgid(arg1);
         break;

    case TARGET_FREEBSD_NR_setpgid:
         ret = do_bsd_setpgid(arg1, arg2);
         break;

    case TARGET_FREEBSD_NR_setreuid:
        ret = do_bsd_setreuid(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setregid:
        ret = do_bsd_setregid(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getresuid:
        ret = do_bsd_getresuid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getresgid:
        ret = do_bsd_getresgid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_setresuid:
        ret = do_bsd_setresuid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_setresgid:
        ret = do_bsd_setresgid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getsid:
        ret = do_bsd_getsid(arg1);
        break;

    case TARGET_FREEBSD_NR_setsid:
        ret = do_bsd_setsid();
        break;

    case TARGET_FREEBSD_NR_issetugid:
        ret = do_bsd_issetugid();
        break;

    case TARGET_FREEBSD_NR_profil:
        ret = do_bsd_profil(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ktrace:
        ret = do_bsd_ktrace(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_setloginclass:
        ret = do_freebsd_setloginclass(arg1);
        break;

    case TARGET_FREEBSD_NR_getloginclass:
        ret = do_freebsd_getloginclass(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_pdgetpid:
        ret = do_freebsd_pdgetpid(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR___setugid:
        ret = do_freebsd___setugid(arg1);
        break;

    case TARGET_FREEBSD_NR_utrace:
        ret = do_bsd_utrace(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_ptrace:
        ret = do_bsd_ptrace(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_getpriority:
        ret = do_bsd_getpriority(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setpriority:
        ret = do_bsd_setpriority(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_procctl:
        ret = do_freebsd_procctl(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_read:
        ret = do_bsd_read(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pread:
        ret = do_bsd_pread(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_readv:
        ret = do_bsd_readv(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_preadv:
        ret = do_bsd_preadv(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_write:
        ret = do_bsd_write(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pwrite:
        ret = do_bsd_pwrite(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_writev:
        ret = do_bsd_writev(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pwritev:
        ret = do_bsd_pwritev(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_open:
        ret = do_bsd_open(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_openat:
        ret = do_bsd_openat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_close:
        ret = do_bsd_close(arg1);
        break;

    case TARGET_FREEBSD_NR_fdatasync:
        ret = do_bsd_fdatasync(arg1);
        break;

    case TARGET_FREEBSD_NR_fsync:
        ret = do_bsd_fsync(arg1);
        break;

    case TARGET_FREEBSD_NR_freebsd12_closefrom:
        ret = do_bsd_closefrom(arg1);
        break;

    case TARGET_FREEBSD_NR_revoke:
        ret = do_bsd_revoke(arg1);
        break;

    case TARGET_FREEBSD_NR_access:
        ret = do_bsd_access(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_eaccess:
        ret = do_bsd_eaccess(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_faccessat:
        ret = do_bsd_faccessat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_chdir:
        ret = do_bsd_chdir(arg1);
        break;

    case TARGET_FREEBSD_NR_fchdir:
        ret = do_bsd_fchdir(arg1);
        break;

    case TARGET_FREEBSD_NR_rename:
        ret = do_bsd_rename(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_renameat:
        ret = do_bsd_renameat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_link:
        ret = do_bsd_link(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_linkat:
        ret = do_bsd_linkat(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_unlink:
        ret = do_bsd_unlink(arg1);
        break;

    case TARGET_FREEBSD_NR_unlinkat:
        ret = do_bsd_unlinkat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mkdir:
        ret = do_bsd_mkdir(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mkdirat:
        ret = do_bsd_mkdirat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_rmdir:
        ret = do_bsd_rmdir(arg1);
        break;

    case TARGET_FREEBSD_NR___getcwd:
        ret = do_bsd___getcwd(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_dup:
        ret = do_bsd_dup(arg1);
        break;

    case TARGET_FREEBSD_NR_dup2:
        ret = do_bsd_dup2(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_truncate:
        ret = do_bsd_truncate(cpu_env, arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ftruncate:
        ret = do_bsd_ftruncate(cpu_env, arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_acct:
        ret = do_bsd_acct(arg1);
        break;

    case TARGET_FREEBSD_NR_sync:
        ret = do_bsd_sync();
        break;

    case TARGET_FREEBSD_NR_mount:
        ret = do_bsd_mount(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_unmount:
        ret = do_bsd_unmount(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_nmount:
        ret = do_bsd_nmount(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_symlink:
        ret = do_bsd_symlink(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_symlinkat:
        ret = do_bsd_symlinkat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_readlink:
        ret = do_bsd_readlink(cpu_env, arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_readlinkat:
        ret = do_bsd_readlinkat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_chmod:
        ret = do_bsd_chmod(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fchmod:
        ret = do_bsd_fchmod(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lchmod:
        ret = do_bsd_lchmod(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fchmodat:
        ret = do_bsd_fchmodat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_freebsd11_mknod:
        ret = do_bsd_freebsd11_mknod(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_freebsd11_mknodat:
        ret = do_bsd_freebsd11_mknodat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_mknodat:
        ret = do_bsd_mknodat(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_chown:
        ret = do_bsd_chown(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_fchown:
        ret = do_bsd_fchown(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_lchown:
        ret = do_bsd_lchown(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_fchownat:
        ret = do_bsd_fchownat(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_chflags:
        ret = do_bsd_chflags(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lchflags:
        ret = do_bsd_lchflags(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fchflags:
        ret = do_bsd_fchflags(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_chroot:
        ret = do_bsd_chroot(arg1);
        break;

    case TARGET_FREEBSD_NR_flock:
        ret = do_bsd_flock(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mkfifo:
        ret = do_bsd_mkfifo(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mkfifoat:
        ret = do_bsd_mkfifoat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pathconf:
        ret = do_bsd_pathconf(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lpathconf:
        ret = do_bsd_lpathconf(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fpathconf:
        ret = do_bsd_fpathconf(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_undelete:
        ret = do_bsd_undelete(arg1);
        break;

    case TARGET_FREEBSD_NR_freebsd11_stat:
        ret = do_freebsd11_stat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_lstat:
        ret = do_freebsd11_lstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fstat:
        ret = do_freebsd11_fstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fstat:
        ret = do_freebsd_fstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fstatat:
        ret = do_freebsd11_fstatat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_fstatat:
        ret = do_freebsd_fstatat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_freebsd11_nstat:
        ret = do_freebsd11_nstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_nfstat:
        ret = do_freebsd11_nfstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_nlstat:
        ret = do_freebsd11_nlstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getfh:
        ret = do_freebsd_getfh(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lgetfh:
        ret = do_freebsd_lgetfh(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fhopen:
        ret = do_freebsd_fhopen(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fhstat:
        ret = do_freebsd11_fhstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fhstat:
        ret = do_freebsd_fhstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fhstatfs:
        ret = do_freebsd11_fhstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fhstatfs:
        ret = do_freebsd_fhstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_statfs:
        ret = do_freebsd11_statfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_statfs:
        ret = do_freebsd_statfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fstatfs:
        ret = do_freebsd11_fstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fstatfs:
        ret = do_freebsd_fstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_getfsstat:
        ret = do_freebsd11_getfsstat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getfsstat:
        ret = do_freebsd_getfsstat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_freebsd11_getdents:
        ret = do_freebsd11_getdents(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getdirentries:
        ret = do_freebsd_getdirentries(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_freebsd11_getdirentries:
        ret = do_freebsd11_getdirentries(arg1, arg2, arg3, arg4);
        break;
    case TARGET_FREEBSD_NR_fcntl:
        ret = do_freebsd_fcntl(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mmap:
        ret = do_bsd_mmap(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                          arg8);
        break;

    case TARGET_FREEBSD_NR_munmap:
        ret = do_bsd_munmap(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mprotect:
        ret = do_bsd_mprotect(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_msync:
        ret = do_bsd_msync(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mlock:
        ret = do_bsd_mlock(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_munlock:
        ret = do_bsd_munlock(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mlockall:
        ret = do_bsd_mlockall(arg1);
        break;

    case TARGET_FREEBSD_NR_munlockall:
        ret = do_bsd_munlockall();
        break;

    case TARGET_FREEBSD_NR_madvise:
        ret = do_bsd_madvise(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_minherit:
        ret = do_bsd_minherit(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mincore:
        ret = do_bsd_mincore(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_freebsd12_shm_open:
        ret = do_bsd_shm_open(arg1, arg2, arg3);
        break;

#if defined(__FreeBSD_version) && __FreeBSD_version >= 1300048
    case TARGET_FREEBSD_NR_shm_open2:
        ret = do_freebsd_shm_open2(arg1, arg2, arg3, arg4, arg5);
        break;
#endif

#if defined(__FreeBSD_version) && __FreeBSD_version >= 1300049
    case TARGET_FREEBSD_NR_shm_rename:
        ret = do_freebsd_shm_rename(arg1, arg2, arg3);
        break;
#endif

    case TARGET_FREEBSD_NR_shm_unlink:
        ret = do_bsd_shm_unlink(arg1);
        break;

    case TARGET_FREEBSD_NR_shmget:
        ret = do_bsd_shmget(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_shmctl:
        ret = do_bsd_shmctl(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_shmat:
        ret = do_bsd_shmat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_shmdt:
        ret = do_bsd_shmdt(arg1);
        break;

    case TARGET_FREEBSD_NR_freebsd11_vadvise:
        ret = do_bsd_vadvise();
        break;

    case TARGET_FREEBSD_NR_sbrk:
        ret = do_bsd_sbrk();
        break;

    case TARGET_FREEBSD_NR_sstk:
        ret = do_bsd_sstk();
        break;

    case TARGET_FREEBSD_NR_break:
        ret = do_obreak(arg1);
        break;

    case TARGET_FREEBSD_NR___sysctl:
        ret = do_freebsd_sysctl(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR___sysctlbyname:
        ret = do_freebsd_sysctlbyname(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_sysarch:
        ret = do_freebsd_sysarch(cpu_env, arg1, arg2);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported syscall: %d\n", num);
        ret = -TARGET_ENOSYS;
        break;
    }

    return ret;
}

abi_long do_freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    abi_long ret;

    if (do_strace) {
        print_freebsd_syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    ret = freebsd_syscall(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6,
                          arg7, arg8);
    if (do_strace) {
        print_freebsd_syscall_ret(num, ret);
    }

    return ret;
}

void syscall_init(void)
{
}
