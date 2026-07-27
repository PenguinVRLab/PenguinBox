/*
 * Present a block device as a raw image through FUSE
 *
 * Copyright (c) 2020 Max Reitz <mreitz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 or later of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define FUSE_USE_VERSION 31

#include "qemu/osdep.h"
#include "qemu/memalign.h"
#include "block/aio.h"
#include "block/block_int-common.h"
#include "block/export.h"
#include "block/fuse.h"
#include "block/qapi.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "qemu/main-loop.h"
#include "system/block-backend.h"

#include <fuse.h>
#include <fuse_lowlevel.h>

#if defined(CONFIG_FALLOCATE_ZERO_RANGE)
#include <linux/falloc.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

#define FUSE_MAX_BOUNCE_BYTES (MIN(BDRV_REQUEST_MAX_BYTES, 64 * 1024 * 1024))


typedef struct FuseExport {
    BlockExport common;

    struct fuse_session *fuse_session;
    struct fuse_buf fuse_buf;
    unsigned int in_flight;
    bool mounted, fd_handler_set_up;

    char *mountpoint;
    bool writable;
    bool growable;
    bool allow_other;

    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
} FuseExport;

static GHashTable *exports;
static const struct fuse_lowlevel_ops fuse_ops;

static void fuse_export_shutdown(BlockExport *exp);
static void fuse_export_delete(BlockExport *exp);

static void init_exports_table(void);

static int setup_fuse_export(FuseExport *exp, const char *mountpoint,
                             bool allow_other, Error **errp);
static void read_from_fuse_export(void *opaque);

static bool is_regular_file(const char *path, Error **errp);


static void fuse_export_drained_begin(void *opaque)
{
    FuseExport *exp = opaque;

    aio_set_fd_handler(exp->common.ctx,
                       fuse_session_fd(exp->fuse_session),
                       NULL, NULL, NULL, NULL, NULL);
    exp->fd_handler_set_up = false;
}

static void fuse_export_drained_end(void *opaque)
{
    FuseExport *exp = opaque;

    exp->common.ctx = blk_get_aio_context(exp->common.blk);

    aio_set_fd_handler(exp->common.ctx,
                       fuse_session_fd(exp->fuse_session),
                       read_from_fuse_export, NULL, NULL, NULL, exp);
    exp->fd_handler_set_up = true;
}

static bool fuse_export_drained_poll(void *opaque)
{
    FuseExport *exp = opaque;

    return qatomic_read(&exp->in_flight) > 0;
}

static const BlockDevOps fuse_export_blk_dev_ops = {
    .drained_begin = fuse_export_drained_begin,
    .drained_end   = fuse_export_drained_end,
    .drained_poll  = fuse_export_drained_poll,
};

static int fuse_export_create(BlockExport *blk_exp,
                              BlockExportOptions *blk_exp_args,
                              Error **errp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);
    BlockExportOptionsFuse *args = &blk_exp_args->u.fuse;
    int ret;

    assert(blk_exp_args->type == BLOCK_EXPORT_TYPE_FUSE);

    if (args->growable || blk_exp_args->writable) {
        uint64_t blk_perm, blk_shared_perm;

        blk_get_perm(exp->common.blk, &blk_perm, &blk_shared_perm);

        ret = blk_set_perm(exp->common.blk, blk_perm | BLK_PERM_RESIZE,
                           blk_shared_perm, errp);
        if (ret < 0) {
            return ret;
        }
    }

    blk_set_dev_ops(exp->common.blk, &fuse_export_blk_dev_ops, exp);

    blk_set_disable_request_queuing(exp->common.blk, true);

    init_exports_table();

    if (g_hash_table_contains(exports, args->mountpoint)) {
        error_setg(errp, "There already is a FUSE export on '%s'",
                   args->mountpoint);
        ret = -EEXIST;
        goto fail;
    }

    if (!is_regular_file(args->mountpoint, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    exp->mountpoint = g_strdup(args->mountpoint);
    exp->writable = blk_exp_args->writable;
    exp->growable = args->growable;

    if (!args->has_allow_other) {
        args->allow_other = FUSE_EXPORT_ALLOW_OTHER_AUTO;
    }

    exp->st_mode = S_IFREG | S_IRUSR;
    if (exp->writable) {
        exp->st_mode |= S_IWUSR;
    }
    exp->st_uid = getuid();
    exp->st_gid = getgid();

    if (args->allow_other == FUSE_EXPORT_ALLOW_OTHER_AUTO) {
        ret = setup_fuse_export(exp, args->mountpoint, true, NULL);
        exp->allow_other = ret == 0;
        if (ret < 0) {
            ret = setup_fuse_export(exp, args->mountpoint, false, errp);
        }
    } else {
        exp->allow_other = args->allow_other == FUSE_EXPORT_ALLOW_OTHER_ON;
        ret = setup_fuse_export(exp, args->mountpoint, exp->allow_other, errp);
    }
    if (ret < 0) {
        goto fail;
    }

    return 0;

fail:
    fuse_export_delete(blk_exp);
    return ret;
}

static void init_exports_table(void)
{
    if (exports) {
        return;
    }

    exports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

static int setup_fuse_export(FuseExport *exp, const char *mountpoint,
                             bool allow_other, Error **errp)
{
    const char *fuse_argv[4];
    char *mount_opts;
    struct fuse_args fuse_args;
    int ret;

    mount_opts = g_strdup_printf("max_read=%zu,default_permissions%s",
                                 FUSE_MAX_BOUNCE_BYTES,
                                 allow_other ? ",allow_other" : "");

    fuse_argv[0] = "";
    fuse_argv[1] = "-o";
    fuse_argv[2] = mount_opts;
    fuse_argv[3] = NULL;
    fuse_args = (struct fuse_args)FUSE_ARGS_INIT(3, (char **)fuse_argv);

    exp->fuse_session = fuse_session_new(&fuse_args, &fuse_ops,
                                         sizeof(fuse_ops), exp);
    g_free(mount_opts);
    if (!exp->fuse_session) {
        error_setg(errp, "Failed to set up FUSE session");
        ret = -EIO;
        goto fail;
    }

    ret = fuse_session_mount(exp->fuse_session, mountpoint);
    if (ret < 0) {
        error_setg(errp, "Failed to mount FUSE session to export");
        ret = -EIO;
        goto fail;
    }
    exp->mounted = true;

    g_hash_table_insert(exports, g_strdup(mountpoint), NULL);

    aio_set_fd_handler(exp->common.ctx,
                       fuse_session_fd(exp->fuse_session),
                       read_from_fuse_export, NULL, NULL, NULL, exp);
    exp->fd_handler_set_up = true;

    return 0;

fail:
    fuse_export_shutdown(&exp->common);
    return ret;
}

static void read_from_fuse_export(void *opaque)
{
    FuseExport *exp = opaque;
    int ret;

    blk_exp_ref(&exp->common);

    qatomic_inc(&exp->in_flight);

    do {
        ret = fuse_session_receive_buf(exp->fuse_session, &exp->fuse_buf);
    } while (ret == -EINTR);
    if (ret < 0) {
        goto out;
    }

    fuse_session_process_buf(exp->fuse_session, &exp->fuse_buf);

out:
    if (qatomic_fetch_dec(&exp->in_flight) == 1) {
        aio_wait_kick();
    }

    blk_exp_unref(&exp->common);
}

static void fuse_export_shutdown(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    if (exp->fuse_session) {
        fuse_session_exit(exp->fuse_session);

        if (exp->fd_handler_set_up) {
            aio_set_fd_handler(exp->common.ctx,
                               fuse_session_fd(exp->fuse_session),
                               NULL, NULL, NULL, NULL, NULL);
            exp->fd_handler_set_up = false;
        }
    }

    if (exp->mountpoint) {
        g_hash_table_remove(exports, exp->mountpoint);
    }
}

static void fuse_export_delete(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    if (exp->fuse_session) {
        if (exp->mounted) {
            fuse_session_unmount(exp->fuse_session);
        }

        fuse_session_destroy(exp->fuse_session);
    }

    free(exp->fuse_buf.mem);
    g_free(exp->mountpoint);
}

static bool is_regular_file(const char *path, Error **errp)
{
    struct stat statbuf;
    int ret;

    ret = stat(path, &statbuf);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Failed to stat '%s'", path);
        return false;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        error_setg(errp, "'%s' is not a regular file", path);
        return false;
    }

    return true;
}

static void fuse_init(void *userdata, struct fuse_conn_info *conn)
{
    conn->max_read = FUSE_MAX_BOUNCE_BYTES;

    conn->max_write = MIN_NON_ZERO(BDRV_REQUEST_MAX_BYTES, conn->max_write);
}

static void fuse_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    fuse_reply_err(req, ENOENT);
}

static void fuse_getattr(fuse_req_t req, fuse_ino_t inode,
                         struct fuse_file_info *fi)
{
    struct stat statbuf;
    int64_t length, allocated_blocks;
    time_t now = time(NULL);
    FuseExport *exp = fuse_req_userdata(req);

    length = blk_getlength(exp->common.blk);
    if (length < 0) {
        fuse_reply_err(req, -length);
        return;
    }

    allocated_blocks = bdrv_get_allocated_file_size(blk_bs(exp->common.blk));
    if (allocated_blocks <= 0) {
        allocated_blocks = DIV_ROUND_UP(length, 512);
    } else {
        allocated_blocks = DIV_ROUND_UP(allocated_blocks, 512);
    }

    statbuf = (struct stat) {
        .st_ino     = inode,
        .st_mode    = exp->st_mode,
        .st_nlink   = 1,
        .st_uid     = exp->st_uid,
        .st_gid     = exp->st_gid,
        .st_size    = length,
        .st_blksize = blk_bs(exp->common.blk)->bl.request_alignment,
        .st_blocks  = allocated_blocks,
        .st_atime   = now,
        .st_mtime   = now,
        .st_ctime   = now,
    };

    fuse_reply_attr(req, &statbuf, 1.);
}

static int fuse_do_truncate(const FuseExport *exp, int64_t size,
                            bool req_zero_write, PreallocMode prealloc)
{
    uint64_t blk_perm, blk_shared_perm;
    BdrvRequestFlags truncate_flags = 0;
    bool add_resize_perm;
    int ret, ret_check;

    add_resize_perm = !exp->growable && !exp->writable;

    if (req_zero_write) {
        truncate_flags |= BDRV_REQ_ZERO_WRITE;
    }

    if (add_resize_perm) {

        if (!qemu_in_main_thread()) {
            return -EPERM;
        }

        blk_get_perm(exp->common.blk, &blk_perm, &blk_shared_perm);

        ret = blk_set_perm(exp->common.blk, blk_perm | BLK_PERM_RESIZE,
                           blk_shared_perm, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    ret = blk_truncate(exp->common.blk, size, true, prealloc,
                       truncate_flags, NULL);

    if (add_resize_perm) {
        ret_check = blk_set_perm(exp->common.blk, blk_perm,
                                 blk_shared_perm, &error_abort);
        assert(ret_check == 0);
    }

    return ret;
}

static void fuse_setattr(fuse_req_t req, fuse_ino_t inode, struct stat *statbuf,
                         int to_set, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int supported_attrs;
    int ret;

    supported_attrs = FUSE_SET_ATTR_SIZE | FUSE_SET_ATTR_MODE;
    if (exp->allow_other) {
        supported_attrs |= FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID;
    }

    if (to_set & ~supported_attrs) {
        fuse_reply_err(req, ENOTSUP);
        return;
    }

    if (to_set & FUSE_SET_ATTR_MODE) {
        if (!exp->allow_other &&
            (statbuf->st_mode & (S_IRWXG | S_IRWXO)) != 0)
        {
            fuse_reply_err(req, EPERM);
            return;
        }

        if (!exp->writable &&
            (statbuf->st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0)
        {
            fuse_reply_err(req, EROFS);
            return;
        }
    }

    if (to_set & FUSE_SET_ATTR_SIZE) {
        if (!exp->writable) {
            fuse_reply_err(req, EACCES);
            return;
        }

        ret = fuse_do_truncate(exp, statbuf->st_size, true, PREALLOC_MODE_OFF);
        if (ret < 0) {
            fuse_reply_err(req, -ret);
            return;
        }
    }

    if (to_set & FUSE_SET_ATTR_MODE) {
        exp->st_mode = (statbuf->st_mode & 07777) | S_IFREG;
    }

    if (to_set & FUSE_SET_ATTR_UID) {
        exp->st_uid = statbuf->st_uid;
    }

    if (to_set & FUSE_SET_ATTR_GID) {
        exp->st_gid = statbuf->st_gid;
    }

    fuse_getattr(req, inode, fi);
}

static void fuse_open(fuse_req_t req, fuse_ino_t inode,
                      struct fuse_file_info *fi)
{
    fuse_reply_open(req, fi);
}

static void fuse_read(fuse_req_t req, fuse_ino_t inode,
                      size_t size, off_t offset, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int64_t length;
    void *buf;
    int ret;

    if (size > FUSE_MAX_BOUNCE_BYTES) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    length = blk_getlength(exp->common.blk);
    if (length < 0) {
        fuse_reply_err(req, -length);
        return;
    }

    if (offset + size > length) {
        size = length - offset;
    }

    buf = qemu_try_blockalign(blk_bs(exp->common.blk), size);
    if (!buf) {
        fuse_reply_err(req, ENOMEM);
        return;
    }

    ret = blk_pread(exp->common.blk, offset, size, buf, 0);
    if (ret >= 0) {
        fuse_reply_buf(req, buf, size);
    } else {
        fuse_reply_err(req, -ret);
    }

    qemu_vfree(buf);
}

static void fuse_write(fuse_req_t req, fuse_ino_t inode, const char *buf,
                       size_t size, off_t offset, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int64_t length;
    int ret;

    if (size > BDRV_REQUEST_MAX_BYTES) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (!exp->writable) {
        fuse_reply_err(req, EACCES);
        return;
    }

    length = blk_getlength(exp->common.blk);
    if (length < 0) {
        fuse_reply_err(req, -length);
        return;
    }

    if (offset + size > length) {
        if (exp->growable) {
            ret = fuse_do_truncate(exp, offset + size, true, PREALLOC_MODE_OFF);
            if (ret < 0) {
                fuse_reply_err(req, -ret);
                return;
            }
        } else {
            size = length - offset;
        }
    }

    ret = blk_pwrite(exp->common.blk, offset, size, buf, 0);
    if (ret >= 0) {
        fuse_reply_write(req, size);
    } else {
        fuse_reply_err(req, -ret);
    }
}

static void fuse_fallocate(fuse_req_t req, fuse_ino_t inode, int mode,
                           off_t offset, off_t length,
                           struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int64_t blk_len;
    int ret;

    if (!exp->writable) {
        fuse_reply_err(req, EACCES);
        return;
    }

    blk_len = blk_getlength(exp->common.blk);
    if (blk_len < 0) {
        fuse_reply_err(req, -blk_len);
        return;
    }

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    if (mode & FALLOC_FL_KEEP_SIZE) {
        length = MIN(length, blk_len - offset);
    }
#endif

    if (!mode) {
        if (offset < blk_len) {
            fuse_reply_err(req, EOPNOTSUPP);
            return;
        }

        if (offset > blk_len) {
            ret = fuse_do_truncate(exp, offset, true, PREALLOC_MODE_OFF);
            if (ret < 0) {
                fuse_reply_err(req, -ret);
                return;
            }
        }

        ret = fuse_do_truncate(exp, offset + length, true,
                               PREALLOC_MODE_FALLOC);
    }
#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    else if (mode & FALLOC_FL_PUNCH_HOLE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE)) {
            fuse_reply_err(req, EINVAL);
            return;
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_pwrite_zeroes(exp->common.blk, offset, size,
                                    BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK);
            if (ret == -ENOTSUP) {
                ret = -EOPNOTSUPP;
            }

            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif
#ifdef CONFIG_FALLOCATE_ZERO_RANGE
    else if (mode & FALLOC_FL_ZERO_RANGE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + length > blk_len) {
            ret = fuse_do_truncate(exp, offset + length, false,
                                   PREALLOC_MODE_OFF);
            if (ret < 0) {
                fuse_reply_err(req, -ret);
                return;
            }
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_pwrite_zeroes(exp->common.blk,
                                    offset, size, 0);
            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif
    else {
        ret = -EOPNOTSUPP;
    }

    fuse_reply_err(req, ret < 0 ? -ret : 0);
}

static void fuse_fsync(fuse_req_t req, fuse_ino_t inode, int datasync,
                       struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int ret;

    ret = blk_flush(exp->common.blk);
    fuse_reply_err(req, ret < 0 ? -ret : 0);
}

static void fuse_flush(fuse_req_t req, fuse_ino_t inode,
                        struct fuse_file_info *fi)
{
    fuse_fsync(req, inode, 1, fi);
}

#ifdef CONFIG_FUSE_LSEEK
static void fuse_lseek(fuse_req_t req, fuse_ino_t inode, off_t offset,
                       int whence, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);

    if (whence != SEEK_HOLE && whence != SEEK_DATA) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    while (true) {
        int64_t pnum;
        int ret;

        ret = bdrv_block_status_above(blk_bs(exp->common.blk), NULL,
                                      offset, INT64_MAX, &pnum, NULL, NULL);
        if (ret < 0) {
            fuse_reply_err(req, -ret);
            return;
        }

        if (!pnum && (ret & BDRV_BLOCK_EOF)) {
            int64_t blk_len;

            blk_len = blk_getlength(exp->common.blk);
            if (blk_len < 0) {
                fuse_reply_err(req, -blk_len);
                return;
            }

            if (offset > blk_len || whence == SEEK_DATA) {
                fuse_reply_err(req, ENXIO);
            } else {
                fuse_reply_lseek(req, offset);
            }
            return;
        }

        if (ret & BDRV_BLOCK_DATA) {
            if (whence == SEEK_DATA) {
                fuse_reply_lseek(req, offset);
                return;
            }
        } else {
            if (whence == SEEK_HOLE) {
                fuse_reply_lseek(req, offset);
                return;
            }
        }

        if (!pnum) {
            fuse_reply_err(req, ENXIO);
            return;
        }

        offset += pnum;
    }
}
#endif

static const struct fuse_lowlevel_ops fuse_ops = {
    .init       = fuse_init,
    .lookup     = fuse_lookup,
    .getattr    = fuse_getattr,
    .setattr    = fuse_setattr,
    .open       = fuse_open,
    .read       = fuse_read,
    .write      = fuse_write,
    .fallocate  = fuse_fallocate,
    .flush      = fuse_flush,
    .fsync      = fuse_fsync,
#ifdef CONFIG_FUSE_LSEEK
    .lseek      = fuse_lseek,
#endif
};

const BlockExportDriver blk_exp_fuse = {
    .type               = BLOCK_EXPORT_TYPE_FUSE,
    .instance_size      = sizeof(FuseExport),
    .create             = fuse_export_create,
    .delete             = fuse_export_delete,
    .request_shutdown   = fuse_export_shutdown,
};
