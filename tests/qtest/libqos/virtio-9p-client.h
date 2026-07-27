/*
 * 9P network client for VirtIO 9P test cases (based on QTest)
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#ifndef TESTS_LIBQOS_VIRTIO_9P_CLIENT_H
#define TESTS_LIBQOS_VIRTIO_9P_CLIENT_H

#include "hw/9pfs/9p.h"
#include "hw/9pfs/9p-synth.h"
#include "virtio-9p.h"
#include "qgraph.h"
#include "tests/qtest/libqtest-single.h"

#define P9_MAX_SIZE 4096

typedef struct {
    QTestState *qts;
    QVirtio9P *v9p;
    uint16_t tag;
    uint64_t t_msg;
    uint32_t t_size;
    uint64_t r_msg;
    size_t t_off;
    size_t r_off;
    uint32_t free_head;
} P9Req;

typedef char v9fs_qid[13];

typedef struct v9fs_attr {
    uint64_t valid;
    v9fs_qid qid;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t nlink;
    uint64_t rdev;
    uint64_t size;
    uint64_t blksize;
    uint64_t blocks;
    uint64_t atime_sec;
    uint64_t atime_nsec;
    uint64_t mtime_sec;
    uint64_t mtime_nsec;
    uint64_t ctime_sec;
    uint64_t ctime_nsec;
    uint64_t btime_sec;
    uint64_t btime_nsec;
    uint64_t gen;
    uint64_t data_version;
} v9fs_attr;

#define P9_GETATTR_BASIC    0x000007ffULL
#define P9_GETATTR_ALL      0x00003fffULL

#define P9_SETATTR_MODE         0x00000001UL
#define P9_SETATTR_UID          0x00000002UL
#define P9_SETATTR_GID          0x00000004UL
#define P9_SETATTR_SIZE         0x00000008UL
#define P9_SETATTR_ATIME        0x00000010UL
#define P9_SETATTR_MTIME        0x00000020UL
#define P9_SETATTR_CTIME        0x00000040UL
#define P9_SETATTR_ATIME_SET    0x00000080UL
#define P9_SETATTR_MTIME_SET    0x00000100UL

struct V9fsDirent {
    v9fs_qid qid;
    uint64_t offset;
    uint8_t type;
    char *name;
    struct V9fsDirent *next;
};

typedef struct TWalkOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    uint32_t newfid;
    uint16_t nwname;
    char **wnames;
    const char *path;
    struct {
        uint16_t *nwqid;
        v9fs_qid **wqid;
    } rwalk;
    bool requestOnly;
    uint32_t expectErr;
} TWalkOpt;

typedef struct TWalkRes {
    uint32_t newfid;
    P9Req *req;
} TWalkRes;

typedef struct TVersionOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t msize;
    const char *version;
    bool requestOnly;
    uint32_t expectErr;
} TVersionOpt;

typedef struct TVersionRes {
    P9Req *req;
} TVersionRes;

typedef struct TAttachOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    uint32_t n_uname;
    struct {
        v9fs_qid *qid;
    } rattach;
    bool requestOnly;
    uint32_t expectErr;
} TAttachOpt;

typedef struct TAttachRes {
    P9Req *req;
} TAttachRes;

typedef struct TGetAttrOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    uint64_t request_mask;
    struct {
        v9fs_attr *attr;
    } rgetattr;
    bool requestOnly;
    uint32_t expectErr;
} TGetAttrOpt;

typedef struct TGetAttrRes {
    P9Req *req;
} TGetAttrRes;

typedef struct TSetAttrOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    v9fs_attr attr;
    bool requestOnly;
    uint32_t expectErr;
} TSetAttrOpt;

typedef struct TSetAttrRes {
    P9Req *req;
} TSetAttrRes;

typedef struct TReadDirOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    uint64_t offset;
    uint32_t count;
    struct {
        uint32_t *count;
        uint32_t *nentries;
        struct V9fsDirent **entries;
    } rreaddir;
    bool requestOnly;
    uint32_t expectErr;
} TReadDirOpt;

typedef struct TReadDirRes {
    P9Req *req;
} TReadDirRes;

typedef struct TLOpenOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    uint32_t flags;
    struct {
        v9fs_qid *qid;
        uint32_t *iounit;
    } rlopen;
    bool requestOnly;
    uint32_t expectErr;
} TLOpenOpt;

typedef struct TLOpenRes {
    P9Req *req;
} TLOpenRes;

typedef struct TWriteOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    uint64_t offset;
    uint32_t count;
    const void *data;
    bool requestOnly;
    uint32_t expectErr;
} TWriteOpt;

typedef struct TWriteRes {
    P9Req *req;
    uint32_t count;
} TWriteRes;

typedef struct TFlushOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint16_t oldtag;
    bool requestOnly;
    uint32_t expectErr;
} TFlushOpt;

typedef struct TFlushRes {
    P9Req *req;
} TFlushRes;

typedef struct TMkdirOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t dfid;
    const char *atPath;
    const char *name;
    uint32_t mode;
    uint32_t gid;
    struct {
        v9fs_qid *qid;
    } rmkdir;
    bool requestOnly;
    uint32_t expectErr;
} TMkdirOpt;

typedef struct TMkdirRes {
    P9Req *req;
} TMkdirRes;

typedef struct TlcreateOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    const char *atPath;
    const char *name;
    uint32_t flags;
    uint32_t mode;
    uint32_t gid;
    struct {
        v9fs_qid *qid;
        uint32_t *iounit;
    } rlcreate;
    bool requestOnly;
    uint32_t expectErr;
} TlcreateOpt;

typedef struct TlcreateRes {
    P9Req *req;
} TlcreateRes;

typedef struct TsymlinkOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t fid;
    const char *atPath;
    const char *name;
    const char *symtgt;
    uint32_t gid;
    struct {
        v9fs_qid *qid;
    } rsymlink;
    bool requestOnly;
    uint32_t expectErr;
} TsymlinkOpt;

typedef struct TsymlinkRes {
    P9Req *req;
} TsymlinkRes;

typedef struct TlinkOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t dfid;
    const char *atPath;
    uint32_t fid;
    const char *toPath;
    const char *name;
    bool requestOnly;
    uint32_t expectErr;
} TlinkOpt;

typedef struct TlinkRes {
    P9Req *req;
} TlinkRes;

typedef struct TunlinkatOpt {
    QVirtio9P *client;
    uint16_t tag;
    uint32_t dirfd;
    const char *atPath;
    const char *name;
    uint32_t flags;
    bool requestOnly;
    uint32_t expectErr;
} TunlinkatOpt;

typedef struct TunlinkatRes {
    P9Req *req;
} TunlinkatRes;

void v9fs_set_allocator(QGuestAllocator *t_alloc);
void v9fs_memwrite(P9Req *req, const void *addr, size_t len);
void v9fs_memskip(P9Req *req, size_t len);
void v9fs_memread(P9Req *req, void *addr, size_t len);
void v9fs_uint8_read(P9Req *req, uint8_t *val);
void v9fs_uint16_write(P9Req *req, uint16_t val);
void v9fs_uint16_read(P9Req *req, uint16_t *val);
void v9fs_uint32_write(P9Req *req, uint32_t val);
void v9fs_uint64_write(P9Req *req, uint64_t val);
void v9fs_uint32_read(P9Req *req, uint32_t *val);
void v9fs_uint64_read(P9Req *req, uint64_t *val);
uint16_t v9fs_string_size(const char *string);
void v9fs_string_write(P9Req *req, const char *string);
void v9fs_string_read(P9Req *req, uint16_t *len, char **string);
P9Req *v9fs_req_init(QVirtio9P *v9p, uint32_t size, uint8_t id,
                     uint16_t tag);
void v9fs_req_send(P9Req *req);
void v9fs_req_wait_for_reply(P9Req *req, uint32_t *len);
void v9fs_req_recv(P9Req *req, uint8_t id);
void v9fs_req_free(P9Req *req);
void v9fs_rlerror(P9Req *req, uint32_t *err);
TVersionRes v9fs_tversion(TVersionOpt);
void v9fs_rversion(P9Req *req, uint16_t *len, char **version);
TAttachRes v9fs_tattach(TAttachOpt);
void v9fs_rattach(P9Req *req, v9fs_qid *qid);
TWalkRes v9fs_twalk(TWalkOpt opt);
void v9fs_rwalk(P9Req *req, uint16_t *nwqid, v9fs_qid **wqid);
TGetAttrRes v9fs_tgetattr(TGetAttrOpt);
void v9fs_rgetattr(P9Req *req, v9fs_attr *attr);
TSetAttrRes v9fs_tsetattr(TSetAttrOpt opt);
void v9fs_rsetattr(P9Req *req);
TReadDirRes v9fs_treaddir(TReadDirOpt);
void v9fs_rreaddir(P9Req *req, uint32_t *count, uint32_t *nentries,
                   struct V9fsDirent **entries);
void v9fs_free_dirents(struct V9fsDirent *e);
TLOpenRes v9fs_tlopen(TLOpenOpt);
void v9fs_rlopen(P9Req *req, v9fs_qid *qid, uint32_t *iounit);
TWriteRes v9fs_twrite(TWriteOpt);
void v9fs_rwrite(P9Req *req, uint32_t *count);
TFlushRes v9fs_tflush(TFlushOpt);
void v9fs_rflush(P9Req *req);
TMkdirRes v9fs_tmkdir(TMkdirOpt);
void v9fs_rmkdir(P9Req *req, v9fs_qid *qid);
TlcreateRes v9fs_tlcreate(TlcreateOpt);
void v9fs_rlcreate(P9Req *req, v9fs_qid *qid, uint32_t *iounit);
TsymlinkRes v9fs_tsymlink(TsymlinkOpt);
void v9fs_rsymlink(P9Req *req, v9fs_qid *qid);
TlinkRes v9fs_tlink(TlinkOpt);
void v9fs_rlink(P9Req *req);
TunlinkatRes v9fs_tunlinkat(TunlinkatOpt);
void v9fs_runlinkat(P9Req *req);

#endif
