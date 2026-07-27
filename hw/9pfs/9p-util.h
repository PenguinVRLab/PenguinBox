/*
 * 9p utilities
 *
 * Copyright IBM, Corp. 2017
 *
 * Authors:
 *  Greg Kurz <groug@kaod.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_9P_UTIL_H
#define QEMU_9P_UTIL_H

#include "qemu/error-report.h"

#ifdef O_PATH
#define O_PATH_9P_UTIL O_PATH
#else
#define O_PATH_9P_UTIL 0
#endif

#ifdef CONFIG_FREEBSD
#define XATTR_CREATE    0x1
#define XATTR_REPLACE   0x2
#endif

#if !defined(CONFIG_LINUX)

static inline uint64_t makedev_dotl(uint32_t dev_major, uint32_t dev_minor)
{
    uint64_t dev;

    dev  = (((uint64_t) (dev_major & 0x00000fffu)) <<  8);
    dev |= (((uint64_t) (dev_major & 0xfffff000u)) << 32);
    dev |= (((uint64_t) (dev_minor & 0x000000ffu)) <<  0);
    dev |= (((uint64_t) (dev_minor & 0xffffff00u)) << 12);
    return dev;
}

#endif

static inline uint64_t host_dev_to_dotl_dev(dev_t dev)
{
#ifdef CONFIG_LINUX
    return dev;
#else
    return makedev_dotl(major(dev), minor(dev));
#endif
}

static inline int errno_to_dotl(int err) {
#if defined(CONFIG_LINUX)
#elif defined(CONFIG_DARWIN) || defined(CONFIG_FREEBSD)
    if (err == ENAMETOOLONG) {
        err = 36;
    } else if (err == ENOTEMPTY) {
        err = 39;
    } else if (err == ELOOP) {
        err = 40;
    } else if (err == ENOATTR) {
        err = 61;
    } else if (err == ENOTSUP) {
        err = 95;
    } else if (err == EOPNOTSUPP) {
        err = 95;
    }
#else
#error Missing errno translation to Linux for this host system
#endif
    return err;
}

#ifdef CONFIG_DARWIN
#define qemu_fgetxattr(...) fgetxattr(__VA_ARGS__, 0, 0)
#else
#define qemu_fgetxattr fgetxattr
#endif

#define qemu_openat     openat
#define qemu_fstat      fstat
#define qemu_fstatat    fstatat
#define qemu_mkdirat    mkdirat
#define qemu_renameat   renameat
#define qemu_utimensat  utimensat
#define qemu_unlinkat   unlinkat
#define qemu_futimens   futimens

static inline void close_preserve_errno(int fd)
{
    int serrno = errno;
    close(fd);
    errno = serrno;
}

static inline int close_if_special_file(int fd)
{
    struct stat stbuf;

    if (qemu_fstat(fd, &stbuf) < 0) {
        close_preserve_errno(fd);
        return -1;
    }
    if (!S_ISREG(stbuf.st_mode) && !S_ISDIR(stbuf.st_mode)) {
        error_report_once(
            "9p: broken or compromised client detected; attempt to open "
            "special file (i.e. neither regular file, nor directory)"
        );
        close(fd);
        errno = ENXIO;
        return -1;
    }

    return 0;
}

static inline int openat_dir(int dirfd, const char *name)
{
    return qemu_openat(dirfd, name,
                       O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_PATH_9P_UTIL);
}

static inline int openat_file(int dirfd, const char *name, int flags,
                              mode_t mode)
{
    int fd, serrno, ret;

#if !defined(CONFIG_DARWIN) && !defined(CONFIG_FREEBSD)
again:
#endif
    fd = qemu_openat(dirfd, name, flags | O_NOFOLLOW | O_NOCTTY | O_NONBLOCK,
                     mode);
    if (fd == -1) {
#if !defined(CONFIG_DARWIN) && !defined(CONFIG_FREEBSD)
        if (errno == EPERM && (flags & O_NOATIME)) {
            flags &= ~O_NOATIME;
            goto again;
        }
#endif
        return -1;
    }

    if (!(flags & O_PATH_9P_UTIL)) {
        if (close_if_special_file(fd) < 0) {
            return -1;
        }

        serrno = errno;
        ret = fcntl(fd, F_SETFL, flags);
        assert(!ret);
        errno = serrno;
    }
    return fd;
}

#ifdef CONFIG_FREEBSD
ssize_t fgetxattr(int dirfd, const char *name, void *value, size_t size);
#endif
ssize_t fgetxattrat_nofollow(int dirfd, const char *path, const char *name,
                             void *value, size_t size);
int fsetxattrat_nofollow(int dirfd, const char *path, const char *name,
                         void *value, size_t size, int flags);
ssize_t flistxattrat_nofollow(int dirfd, const char *filename,
                              char *list, size_t size);
ssize_t fremovexattrat_nofollow(int dirfd, const char *filename,
                                const char *name);

static inline off_t qemu_dirent_off(struct dirent *dent)
{
#ifdef CONFIG_DARWIN
    return dent->d_seekoff;
#else
    return dent->d_off;
#endif
}

static inline struct dirent *qemu_dirent_dup(struct dirent *dent)
{
    size_t sz = 0;
#if defined _DIRENT_HAVE_D_RECLEN
    sz = dent->d_reclen;
#endif
    if (sz == 0) {
        sz = offsetof(struct dirent, d_name) +
                      strlen(dent->d_name) + 1;
    }
    return g_memdup(dent, sz);
}

#if defined CONFIG_DARWIN && defined CONFIG_PTHREAD_FCHDIR_NP
int pthread_fchdir_np(int fd) __attribute__((weak_import));
#endif
int qemu_mknodat(int dirfd, const char *filename, mode_t mode, dev_t dev);

char *qemu_open_flags_tostr(int flags);

#endif
