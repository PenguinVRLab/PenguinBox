/* Code for loading Linux executables.  Mostly linux kernel code.  */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"
#include "loader.h"
#include "qapi/error.h"

#define NGROUPS 32

abi_long memcpy_to_target(abi_ulong dest, const void *src, unsigned long len)
{
    void *host_ptr;

    host_ptr = lock_user(VERIFY_WRITE, dest, len, 0);
    if (!host_ptr) {
        return -TARGET_EFAULT;
    }
    memcpy(host_ptr, src, len);
    unlock_user(host_ptr, dest, 1);
    return 0;
}

static int count(char **vec)
{
    int i;

    for (i = 0; *vec; i++) {
        vec++;
    }
    return i;
}

static int prepare_binprm(struct linux_binprm *bprm)
{
    struct stat st;
    int mode;
    int retval;

    if (fstat(bprm->src.fd, &st) < 0) {
        return -errno;
    }

    mode = st.st_mode;
    if (!S_ISREG(mode)) {
        return -EACCES;
    }
    if (!(mode & 0111)) {
        return -EACCES;
    }

    bprm->e_uid = geteuid();
    bprm->e_gid = getegid();

    if (mode & S_ISUID) {
        bprm->e_uid = st.st_uid;
    }

    if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
        bprm->e_gid = st.st_gid;
    }

    retval = read(bprm->src.fd, bprm->buf, BPRM_BUF_SIZE);
    if (retval < 0) {
        perror("prepare_binprm");
        exit(-1);
    }
    if (retval < BPRM_BUF_SIZE) {
        memset(bprm->buf + retval, 0, BPRM_BUF_SIZE - retval);
    }

    bprm->src.cache = bprm->buf;
    bprm->src.cache_size = retval;

    return retval;
}

abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr)
{
    TaskState *ts = get_task_state(thread_cpu);
    int n = sizeof(abi_ulong);
    abi_ulong envp;
    abi_ulong argv;

    sp -= (envc + 1) * n;
    envp = sp;
    sp -= (argc + 1) * n;
    argv = sp;
    ts->info->envp = envp;
    ts->info->envc = envc;
    ts->info->argv = argv;
    ts->info->argc = argc;

    if (push_ptr) {
        sp -= n;
        put_user_ual(envp, sp);
        sp -= n;
        put_user_ual(argv, sp);
    }

    sp -= n;
    put_user_ual(argc, sp);

    ts->info->arg_strings = stringp;
    while (argc-- > 0) {
        put_user_ual(stringp, argv);
        argv += n;
        stringp += target_strlen(stringp) + 1;
    }
    put_user_ual(0, argv);

    ts->info->env_strings = stringp;
    while (envc-- > 0) {
        put_user_ual(stringp, envp);
        envp += n;
        stringp += target_strlen(stringp) + 1;
    }
    put_user_ual(0, envp);

    return sp;
}

int loader_exec(int fdexec, const char *filename, char **argv, char **envp,
                struct image_info *infop, struct linux_binprm *bprm)
{
    int retval;

    bprm->src.fd = fdexec;
    bprm->filename = (char *)filename;
    bprm->argc = count(argv);
    bprm->argv = argv;
    bprm->envc = count(envp);
    bprm->envp = envp;

    retval = prepare_binprm(bprm);

    if (retval < 4) {
        return -ENOEXEC;
    }
    if (bprm->buf[0] == 0x7f
        && bprm->buf[1] == 'E'
        && bprm->buf[2] == 'L'
        && bprm->buf[3] == 'F') {
        retval = load_elf_binary(bprm, infop);
#if defined(TARGET_HAS_BFLT)
    } else if (bprm->buf[0] == 'b'
               && bprm->buf[1] == 'F'
               && bprm->buf[2] == 'L'
               && bprm->buf[3] == 'T') {
        retval = load_flt_binary(bprm, infop);
#endif
    } else {
        return -ENOEXEC;
    }
    if (retval < 0) {
        return retval;
    }

    return 0;
}

bool imgsrc_read(void *dst, off_t offset, size_t len,
                 const ImageSource *img, Error **errp)
{
    ssize_t ret;

    if (offset + len <= img->cache_size) {
        memcpy(dst, img->cache + offset, len);
        return true;
    }

    if (img->fd < 0) {
        error_setg(errp, "read past end of buffer");
        return false;
    }

    ret = pread(img->fd, dst, len, offset);
    if (ret == len) {
        return true;
    }
    if (ret < 0) {
        error_setg_errno(errp, errno, "Error reading file header");
    } else {
        error_setg(errp, "Incomplete read of file header");
    }
    return false;
}

void *imgsrc_read_alloc(off_t offset, size_t len,
                        const ImageSource *img, Error **errp)
{
    void *alloc = g_malloc(len);
    bool ok = imgsrc_read(alloc, offset, len, img, errp);

    if (!ok) {
        g_free(alloc);
        alloc = NULL;
    }
    return alloc;
}

abi_long imgsrc_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, const ImageSource *src, abi_ulong offset)
{
    const int prot_write = PROT_READ | PROT_WRITE;
    abi_long ret;
    void *haddr;

    assert(flags == (MAP_PRIVATE | MAP_FIXED));

    if (src->fd >= 0) {
        return target_mmap(start, len, prot, flags, src->fd, offset);
    }

    assert(offset < src->cache_size);

    ret = target_mmap(start, len, prot_write, flags | MAP_ANON, -1, 0);
    if (ret == -1) {
        return ret;
    }

    haddr = lock_user(VERIFY_WRITE, start, len, 0);
    assert(haddr != NULL);
    if (offset + len <= src->cache_size) {
        memcpy(haddr, src->cache + offset, len);
    } else {
        size_t rest = src->cache_size - offset;
        memcpy(haddr, src->cache + offset, rest);
        memset(haddr + rest, 0, len - rest);
    }
    unlock_user(haddr, start, len);

    if (prot != prot_write) {
        target_mprotect(start, len, prot);
    }

    return ret;
}
