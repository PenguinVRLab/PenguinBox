/*
 * QTest testcase for VirtIO 9P
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

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "libqos/virtio-9p-client.h"

#define twalk(...) v9fs_twalk((TWalkOpt) __VA_ARGS__)
#define tversion(...) v9fs_tversion((TVersionOpt) __VA_ARGS__)
#define tattach(...) v9fs_tattach((TAttachOpt) __VA_ARGS__)
#define tgetattr(...) v9fs_tgetattr((TGetAttrOpt) __VA_ARGS__)
#define tsetattr(...) v9fs_tsetattr((TSetAttrOpt) __VA_ARGS__)
#define treaddir(...) v9fs_treaddir((TReadDirOpt) __VA_ARGS__)
#define tlopen(...) v9fs_tlopen((TLOpenOpt) __VA_ARGS__)
#define twrite(...) v9fs_twrite((TWriteOpt) __VA_ARGS__)
#define tflush(...) v9fs_tflush((TFlushOpt) __VA_ARGS__)
#define tmkdir(...) v9fs_tmkdir((TMkdirOpt) __VA_ARGS__)
#define tlcreate(...) v9fs_tlcreate((TlcreateOpt) __VA_ARGS__)
#define tsymlink(...) v9fs_tsymlink((TsymlinkOpt) __VA_ARGS__)
#define tlink(...) v9fs_tlink((TlinkOpt) __VA_ARGS__)
#define tunlinkat(...) v9fs_tunlinkat((TunlinkatOpt) __VA_ARGS__)

static void pci_config(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    size_t tag_len = qvirtio_config_readw(v9p->vdev, 0);
    g_autofree char *tag = NULL;
    int i;

    g_assert_cmpint(tag_len, ==, strlen(MOUNT_TAG));

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(v9p->vdev, i + 2);
    }
    g_assert_cmpmem(tag, tag_len, MOUNT_TAG, tag_len);
}

static inline bool is_same_qid(v9fs_qid a, v9fs_qid b)
{
    return a[0] == b[0] && memcmp(&a[5], &b[5], 8) == 0;
}

static void fs_version(void *obj, void *data, QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    tversion({ .client = obj });
}

static void fs_attach(void *obj, void *data, QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    tattach({ .client = obj });
}

static void fs_walk(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[P9_MAXWELEM];
    uint16_t nwqid;
    g_autofree v9fs_qid *wqid = NULL;
    int i;

    for (i = 0; i < P9_MAXWELEM; i++) {
        wnames[i] = g_strdup_printf(QTEST_V9FS_SYNTH_WALK_FILE, i);
    }

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1,
        .nwname = P9_MAXWELEM, .wnames = wnames,
        .rwalk = { .nwqid = &nwqid, .wqid = &wqid }
    });

    g_assert_cmpint(nwqid, ==, P9_MAXWELEM);

    for (i = 0; i < P9_MAXWELEM; i++) {
        g_free(wnames[i]);
    }
}

static bool fs_dirents_contain_name(struct V9fsDirent *e, const char* name)
{
    for (; e; e = e->next) {
        if (!strcmp(e->name, name)) {
            return true;
        }
    }
    return false;
}

static void fs_readdir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[] = { g_strdup(QTEST_V9FS_SYNTH_READDIR_DIR) };
    uint16_t nqid;
    v9fs_qid qid;
    uint32_t count, nentries;
    struct V9fsDirent *entries = NULL;

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1,
        .nwname = 1, .wnames = wnames, .rwalk.nwqid = &nqid
    });
    g_assert_cmpint(nqid, ==, 1);

    tlopen({
        .client = v9p, .fid = 1, .flags = O_DIRECTORY, .rlopen.qid = &qid
    });

    treaddir({
        .client = v9p, .fid = 1, .offset = 0, .count = P9_MAX_SIZE - 11,
        .rreaddir = {
            .count = &count, .nentries = &nentries, .entries = &entries
        }
    });

    g_assert_cmpint(
        nentries, ==,
        QTEST_V9FS_SYNTH_READDIR_NFILES + 2
    );

    g_assert_cmpint(fs_dirents_contain_name(entries, "."), ==, true);
    g_assert_cmpint(fs_dirents_contain_name(entries, ".."), ==, true);
    for (int i = 0; i < QTEST_V9FS_SYNTH_READDIR_NFILES; ++i) {
        g_autofree char *name =
            g_strdup_printf(QTEST_V9FS_SYNTH_READDIR_FILE, i);
        g_assert_cmpint(fs_dirents_contain_name(entries, name), ==, true);
    }

    v9fs_free_dirents(entries);
    g_free(wnames[0]);
}

static void do_readdir_split(QVirtio9P *v9p, uint32_t count)
{
    char *wnames[] = { g_strdup(QTEST_V9FS_SYNTH_READDIR_DIR) };
    uint16_t nqid;
    v9fs_qid qid;
    uint32_t nentries, npartialentries;
    struct V9fsDirent *entries, *tail, *partialentries;
    int fid;
    uint64_t offset;

    tattach({ .client = v9p });

    fid = 1;
    offset = 0;
    entries = NULL;
    nentries = 0;
    tail = NULL;

    twalk({
        .client = v9p, .fid = 0, .newfid = fid,
        .nwname = 1, .wnames = wnames, .rwalk.nwqid = &nqid
    });
    g_assert_cmpint(nqid, ==, 1);

    tlopen({
        .client = v9p, .fid = fid, .flags = O_DIRECTORY, .rlopen.qid = &qid
    });

    while (true) {
        npartialentries = 0;
        partialentries = NULL;

        treaddir({
            .client = v9p, .fid = fid, .offset = offset, .count = count,
            .rreaddir = {
                .count = &count, .nentries = &npartialentries,
                .entries = &partialentries
            }
        });
        if (npartialentries > 0 && partialentries) {
            if (!entries) {
                entries = partialentries;
                nentries = npartialentries;
                tail = partialentries;
            } else {
                tail->next = partialentries;
                nentries += npartialentries;
            }
            while (tail->next) {
                tail = tail->next;
            }
            offset = tail->offset;
        } else {
            break;
        }
    }

    g_assert_cmpint(
        nentries, ==,
        QTEST_V9FS_SYNTH_READDIR_NFILES + 2
    );

    g_assert_cmpint(fs_dirents_contain_name(entries, "."), ==, true);
    g_assert_cmpint(fs_dirents_contain_name(entries, ".."), ==, true);
    for (int i = 0; i < QTEST_V9FS_SYNTH_READDIR_NFILES; ++i) {
        char *name = g_strdup_printf(QTEST_V9FS_SYNTH_READDIR_FILE, i);
        g_assert_cmpint(fs_dirents_contain_name(entries, name), ==, true);
        g_free(name);
    }

    v9fs_free_dirents(entries);

    g_free(wnames[0]);
}

static void fs_walk_no_slash(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[] = { g_strdup(" /") };

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 1, .wnames = wnames,
        .expectErr = ENOENT
    });

    g_free(wnames[0]);
}

static void fs_walk_nonexistent(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);

    tattach({ .client = v9p });
    twalk({ .client = v9p, .path = "non-existent", .expectErr = ENOENT });
}

static void fs_walk_2nd_nonexistent(void *obj, void *data,
                                    QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    v9fs_qid root_qid;
    uint16_t nwqid;
    uint32_t fid;
    g_autofree v9fs_qid *wqid = NULL;
    g_autofree char *path = g_strdup_printf(
        QTEST_V9FS_SYNTH_WALK_FILE "/non-existent", 0
    );

    tattach({ .client = v9p, .rattach.qid = &root_qid });
    fid = twalk({
        .client = v9p, .path = path,
        .rwalk = { .nwqid = &nwqid, .wqid = &wqid }
    }).newfid;
    assert(nwqid == 1);

    g_assert(wqid && wqid[0] && !is_same_qid(root_qid, wqid[0]));

    tgetattr({
        .client = v9p, .fid = fid, .request_mask = P9_GETATTR_BASIC,
        .expectErr = ENOENT
    });
}

static void fs_walk_none(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    v9fs_qid root_qid;
    g_autofree v9fs_qid *wqid = NULL;
    struct v9fs_attr attr;

    tversion({ .client = v9p });
    tattach({
        .client = v9p, .fid = 0, .n_uname = getuid(),
        .rattach.qid = &root_qid
    });

    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 0, .wnames = NULL,
        .rwalk.wqid = &wqid
    });

    g_assert(wqid == NULL);

    tgetattr({
        .client = v9p, .fid = 1, .request_mask = P9_GETATTR_BASIC,
        .rgetattr.attr = &attr
    });

    g_assert(is_same_qid(root_qid, attr.qid));
}

static void fs_walk_dotdot(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[] = { g_strdup("..") };
    v9fs_qid root_qid;
    g_autofree v9fs_qid *wqid = NULL;

    tversion({ .client = v9p });
    tattach({
        .client = v9p, .fid = 0, .n_uname = getuid(),
        .rattach.qid = &root_qid
    });

    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 1, .wnames = wnames,
        .rwalk.wqid = &wqid
    });

    g_assert_cmpmem(&root_qid, 13, wqid[0], 13);

    g_free(wnames[0]);
}

static void fs_lopen(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[] = { g_strdup(QTEST_V9FS_SYNTH_LOPEN_FILE) };

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 1, .wnames = wnames
    });

    tlopen({ .client = v9p, .fid = 1, .flags = O_WRONLY });

    g_free(wnames[0]);
}

static void fs_write(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    static const uint32_t write_count = P9_MAX_SIZE / 2;
    char *wnames[] = { g_strdup(QTEST_V9FS_SYNTH_WRITE_FILE) };
    g_autofree char *buf = g_malloc0(write_count);
    uint32_t count;

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 1, .wnames = wnames
    });

    tlopen({ .client = v9p, .fid = 1, .flags = O_WRONLY });

    count = twrite({
        .client = v9p, .fid = 1, .offset = 0, .count = write_count,
        .data = buf
    }).count;
    g_assert_cmpint(count, ==, write_count);

    g_free(wnames[0]);
}

static void fs_flush_success(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[] = { g_strdup(QTEST_V9FS_SYNTH_FLUSH_FILE) };
    P9Req *req, *flush_req;
    uint32_t reply_len;
    uint8_t should_block;

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 1, .wnames = wnames
    });

    tlopen({ .client = v9p, .fid = 1, .flags = O_WRONLY });

    should_block = 1;
    req = twrite({
        .client = v9p, .fid = 1, .offset = 0,
        .count = sizeof(should_block), .data = &should_block,
        .requestOnly = true
    }).req;

    flush_req = tflush({
        .client = v9p, .oldtag = req->tag, .tag = 1, .requestOnly = true
    }).req;

    v9fs_req_wait_for_reply(req, &reply_len);
    g_assert_cmpint(reply_len, ==, 0);
    v9fs_req_free(req);
    v9fs_rflush(flush_req);

    g_free(wnames[0]);
}

static void fs_flush_ignored(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[] = { g_strdup(QTEST_V9FS_SYNTH_FLUSH_FILE) };
    P9Req *req, *flush_req;
    uint32_t count;
    uint8_t should_block;

    tattach({ .client = v9p });
    twalk({
        .client = v9p, .fid = 0, .newfid = 1, .nwname = 1, .wnames = wnames
    });

    tlopen({ .client = v9p, .fid = 1, .flags = O_WRONLY });

    should_block = 0;
    req = twrite({
        .client = v9p, .fid = 1, .offset = 0,
        .count = sizeof(should_block), .data = &should_block,
        .requestOnly = true
    }).req;

    flush_req = tflush({
        .client = v9p, .oldtag = req->tag, .tag = 1, .requestOnly = true
    }).req;

    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwrite(req, &count);
    g_assert_cmpint(count, ==, sizeof(should_block));
    v9fs_rflush(flush_req);

    g_free(wnames[0]);
}

static void fs_readdir_split_128(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_readdir_split(obj, 128);
}

static void fs_readdir_split_256(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_readdir_split(obj, 256);
}

static void fs_readdir_split_512(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_readdir_split(obj, 512);
}


static void fs_create_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *root_path = virtio_9p_test_path("");
    g_autofree char *new_dir = virtio_9p_test_path("01");

    g_assert(root_path != NULL);

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "01" });

    g_assert(stat(new_dir, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);
}

static void fs_unlinkat_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *root_path = virtio_9p_test_path("");
    g_autofree char *new_dir = virtio_9p_test_path("02");

    g_assert(root_path != NULL);

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "02" });

    g_assert(stat(new_dir, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);

    tunlinkat({
        .client = v9p, .atPath = "/", .name = "02",
        .flags = P9_DOTL_AT_REMOVEDIR
    });
    g_assert(stat(new_dir, &st) != 0);
}

static void fs_create_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *new_file = virtio_9p_test_path("03/1st_file");

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "03" });
    tlcreate({ .client = v9p, .atPath = "03", .name = "1st_file" });

    g_assert(stat(new_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);
}

static void fs_unlinkat_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *new_file = virtio_9p_test_path("04/doa_file");

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "04" });
    tlcreate({ .client = v9p, .atPath = "04", .name = "doa_file" });

    g_assert(stat(new_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    tunlinkat({ .client = v9p, .atPath = "04", .name = "doa_file" });
    g_assert(stat(new_file, &st) != 0);
}

static void fs_symlink_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *real_file = virtio_9p_test_path("05/real_file");
    g_autofree char *symlink_file = virtio_9p_test_path("05/symlink_file");

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "05" });
    tlcreate({ .client = v9p, .atPath = "05", .name = "real_file" });
    g_assert(stat(real_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    tsymlink({
        .client = v9p, .atPath = "05", .name = "symlink_file",
        .symtgt = "real_file"
    });

    g_assert(stat(symlink_file, &st) == 0);
}

static void fs_unlinkat_symlink(void *obj, void *data,
                                QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *real_file = virtio_9p_test_path("06/real_file");
    g_autofree char *symlink_file = virtio_9p_test_path("06/symlink_file");

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "06" });
    tlcreate({ .client = v9p, .atPath = "06", .name = "real_file" });
    g_assert(stat(real_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    tsymlink({
        .client = v9p, .atPath = "06", .name = "symlink_file",
        .symtgt = "real_file"
    });
    g_assert(stat(symlink_file, &st) == 0);

    tunlinkat({ .client = v9p, .atPath = "06", .name = "symlink_file" });
    g_assert(stat(symlink_file, &st) != 0);
}

static void fs_hardlink_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st_real, st_link;
    g_autofree char *real_file = virtio_9p_test_path("07/real_file");
    g_autofree char *hardlink_file = virtio_9p_test_path("07/hardlink_file");

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "07" });
    tlcreate({ .client = v9p, .atPath = "07", .name = "real_file" });
    g_assert(stat(real_file, &st_real) == 0);
    g_assert((st_real.st_mode & S_IFMT) == S_IFREG);

    tlink({
        .client = v9p, .atPath = "07", .name = "hardlink_file",
        .toPath = "07/real_file"
    });

    g_assert(stat(hardlink_file, &st_link) == 0);
    g_assert((st_link.st_mode & S_IFMT) == S_IFREG);
    g_assert(st_link.st_dev == st_real.st_dev);
    g_assert(st_link.st_ino == st_real.st_ino);
}

static void fs_unlinkat_hardlink(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st_real, st_link;
    g_autofree char *real_file = virtio_9p_test_path("08/real_file");
    g_autofree char *hardlink_file = virtio_9p_test_path("08/hardlink_file");

    tattach({ .client = v9p });
    tmkdir({ .client = v9p, .atPath = "/", .name = "08" });
    tlcreate({ .client = v9p, .atPath = "08", .name = "real_file" });
    g_assert(stat(real_file, &st_real) == 0);
    g_assert((st_real.st_mode & S_IFMT) == S_IFREG);

    tlink({
        .client = v9p, .atPath = "08", .name = "hardlink_file",
        .toPath = "08/real_file"
    });
    g_assert(stat(hardlink_file, &st_link) == 0);

    tunlinkat({ .client = v9p, .atPath = "08", .name = "hardlink_file" });
    g_assert(stat(hardlink_file, &st_link) != 0);
    g_assert(stat(real_file, &st_real) == 0);
}

static void fs_use_after_unlink(void *obj, void *data,
                                QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    static const uint32_t write_count = P9_MAX_SIZE / 2;
    g_autofree char *real_file = virtio_9p_test_path("09/doa_file");
    g_autofree char *buf = g_malloc0(write_count);
    struct stat st_file;
    struct v9fs_attr attr;
    uint32_t fid_file;
    uint32_t count;

    tattach({ .client = v9p });

    tmkdir({ .client = v9p, .atPath = "/", .name = "09" });
    tlcreate({ .client = v9p, .atPath = "09", .name = "doa_file" });
    g_assert(stat(real_file, &st_file) == 0);
    g_assert((st_file.st_mode & S_IFMT) == S_IFREG);

    fid_file = twalk({
        .client = v9p, .fid = 0, .path = "09/doa_file"
    }).newfid;
    g_assert(fid_file != 0);

    tlopen({ .client = v9p, .fid = fid_file, .flags = O_WRONLY });
    tunlinkat({ .client = v9p, .atPath = "09", .name = "doa_file" });

    tgetattr({
        .client = v9p, .fid = fid_file, .request_mask = P9_GETATTR_BASIC,
        .rgetattr.attr = &attr
    });
    count = twrite({
        .client = v9p, .fid = fid_file, .offset = 0, .count = write_count,
        .data = buf
    }).count;
    g_assert_cmpint(count, ==, write_count);

    tsetattr({
        .client = v9p, .fid = fid_file, .attr = (v9fs_attr) {
            .valid = P9_SETATTR_SIZE,
            .size = 2001
        }
     });
    tgetattr({
        .client = v9p, .fid = fid_file, .request_mask = P9_GETATTR_BASIC,
        .rgetattr.attr = &attr
    });
    g_assert_cmpint(attr.size, ==, 2001);
}

static void cleanup_9p_local_driver(void *data)
{
    virtio_9p_remove_local_test_dir();
}

static void *assign_9p_local_driver(GString *cmd_line, void *arg)
{
    virtio_9p_create_local_test_dir();

    virtio_9p_assign_local_driver(cmd_line, "security_model=mapped-xattr");

    g_test_queue_destroy(cleanup_9p_local_driver, NULL);
    return arg;
}

static void register_virtio_9p_test(void)
{

    QOSGraphTestOptions opts = {
    };

    qos_add_test("synth/config", "virtio-9p", pci_config, &opts);
    qos_add_test("synth/version/basic", "virtio-9p", fs_version,  &opts);
    qos_add_test("synth/attach/basic", "virtio-9p", fs_attach,  &opts);
    qos_add_test("synth/walk/basic", "virtio-9p", fs_walk,  &opts);
    qos_add_test("synth/walk/no_slash", "virtio-9p", fs_walk_no_slash,
                  &opts);
    qos_add_test("synth/walk/none", "virtio-9p", fs_walk_none, &opts);
    qos_add_test("synth/walk/dotdot_from_root", "virtio-9p",
                 fs_walk_dotdot,  &opts);
    qos_add_test("synth/walk/non_existent", "virtio-9p", fs_walk_nonexistent,
                  &opts);
    qos_add_test("synth/walk/2nd_non_existent", "virtio-9p",
                 fs_walk_2nd_nonexistent, &opts);
    qos_add_test("synth/lopen/basic", "virtio-9p", fs_lopen,  &opts);
    qos_add_test("synth/write/basic", "virtio-9p", fs_write,  &opts);
    qos_add_test("synth/flush/success", "virtio-9p", fs_flush_success,
                  &opts);
    qos_add_test("synth/flush/ignored", "virtio-9p", fs_flush_ignored,
                  &opts);
    qos_add_test("synth/readdir/basic", "virtio-9p", fs_readdir,  &opts);
    qos_add_test("synth/readdir/split_512", "virtio-9p",
                 fs_readdir_split_512,  &opts);
    qos_add_test("synth/readdir/split_256", "virtio-9p",
                 fs_readdir_split_256,  &opts);
    qos_add_test("synth/readdir/split_128", "virtio-9p",
                 fs_readdir_split_128,  &opts);


    opts.before = assign_9p_local_driver;
    qos_add_test("local/config", "virtio-9p", pci_config,  &opts);
    qos_add_test("local/create_dir", "virtio-9p", fs_create_dir, &opts);
    qos_add_test("local/unlinkat_dir", "virtio-9p", fs_unlinkat_dir, &opts);
    qos_add_test("local/create_file", "virtio-9p", fs_create_file, &opts);
    qos_add_test("local/unlinkat_file", "virtio-9p", fs_unlinkat_file, &opts);
    qos_add_test("local/symlink_file", "virtio-9p", fs_symlink_file, &opts);
    qos_add_test("local/unlinkat_symlink", "virtio-9p", fs_unlinkat_symlink,
                 &opts);
    qos_add_test("local/hardlink_file", "virtio-9p", fs_hardlink_file, &opts);
    qos_add_test("local/unlinkat_hardlink", "virtio-9p", fs_unlinkat_hardlink,
                 &opts);
    qos_add_test("local/use_after_unlink", "virtio-9p", fs_use_after_unlink,
                 &opts);
}

libqos_init(register_virtio_9p_test);
