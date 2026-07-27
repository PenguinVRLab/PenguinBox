/*
 * Memory region management for Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/madvise.h"
#include "qemu/mprotect.h"
#include "qemu/memalign.h"
#include "qemu/cacheinfo.h"
#include "qemu/qtree.h"
#include "qapi/error.h"
#include "tcg/tcg.h"
#include "exec/translation-block.h"
#include "tcg-internal.h"
#include "host/cpuinfo.h"


#if defined(_WIN32)
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#endif

struct tcg_region_tree {
    QemuMutex lock;
    QTree *tree;
};

struct tcg_region_state {
    QemuMutex lock;

    void *start_aligned;
    void *after_prologue;
    size_t n;
    size_t size;
    size_t stride;
    size_t total_size;

    size_t current;
    size_t agg_size_full;
};

static struct tcg_region_state region;

static void *region_trees;
static size_t tree_size;

bool in_code_gen_buffer(const void *p)
{
    return (size_t)(p - region.start_aligned) <= region.total_size;
}

#ifndef CONFIG_TCG_INTERPRETER
static int host_prot_read_exec(void)
{
#if defined(CONFIG_LINUX) && defined(HOST_AARCH64) && defined(PROT_BTI)
    if (cpuinfo & CPUINFO_BTI) {
        return PROT_READ | PROT_EXEC | PROT_BTI;
    }
#endif
    return PROT_READ | PROT_EXEC;
}
#endif

#ifdef CONFIG_DEBUG_TCG
const void *tcg_splitwx_to_rx(void *rw)
{
    if (rw) {
        g_assert(in_code_gen_buffer(rw));
        rw += tcg_splitwx_diff;
    }
    return rw;
}

void *tcg_splitwx_to_rw(const void *rx)
{
    if (rx) {
        rx -= tcg_splitwx_diff;
        g_assert(in_code_gen_buffer(rx));
    }
    return (void *)rx;
}
#endif

static int ptr_cmp_tb_tc(const void *ptr, const struct tb_tc *s)
{
    if (ptr >= s->ptr + s->size) {
        return 1;
    } else if (ptr < s->ptr) {
        return -1;
    }
    return 0;
}

static gint tb_tc_cmp(gconstpointer ap, gconstpointer bp, gpointer userdata)
{
    const struct tb_tc *a = ap;
    const struct tb_tc *b = bp;

    if (likely(a->size && b->size)) {
        if (a->ptr > b->ptr) {
            return 1;
        } else if (a->ptr < b->ptr) {
            return -1;
        }
        g_assert(a->size == b->size);
        return 0;
    }
    if (likely(a->size == 0)) {
        return ptr_cmp_tb_tc(a->ptr, b);
    }
    return ptr_cmp_tb_tc(b->ptr, a);
}

static void tb_destroy(gpointer value)
{
    TranslationBlock *tb = value;
    qemu_spin_destroy(&tb->jmp_lock);
}

static void tcg_region_trees_init(void)
{
    size_t i;

    tree_size = ROUND_UP(sizeof(struct tcg_region_tree), qemu_dcache_linesize);
    region_trees = qemu_memalign(qemu_dcache_linesize, region.n * tree_size);
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_init(&rt->lock);
        rt->tree = q_tree_new_full(tb_tc_cmp, NULL, NULL, tb_destroy);
    }
}

static struct tcg_region_tree *tc_ptr_to_region_tree(const void *p)
{
    size_t region_idx;

    if (!in_code_gen_buffer(p)) {
        p -= tcg_splitwx_diff;
        if (!in_code_gen_buffer(p)) {
            return NULL;
        }
    }

    if (p < region.start_aligned) {
        region_idx = 0;
    } else {
        ptrdiff_t offset = p - region.start_aligned;

        if (offset > region.stride * (region.n - 1)) {
            region_idx = region.n - 1;
        } else {
            region_idx = offset / region.stride;
        }
    }
    return region_trees + region_idx * tree_size;
}

void tcg_tb_insert(TranslationBlock *tb)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree(tb->tc.ptr);

    g_assert(rt != NULL);
    qemu_mutex_lock(&rt->lock);
    q_tree_insert(rt->tree, &tb->tc, tb);
    qemu_mutex_unlock(&rt->lock);
}

void tcg_tb_remove(TranslationBlock *tb)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree(tb->tc.ptr);

    g_assert(rt != NULL);
    qemu_mutex_lock(&rt->lock);
    q_tree_remove(rt->tree, &tb->tc);
    qemu_mutex_unlock(&rt->lock);
}

TranslationBlock *tcg_tb_lookup(uintptr_t tc_ptr)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree((void *)tc_ptr);
    TranslationBlock *tb;
    struct tb_tc s = { .ptr = (void *)tc_ptr };

    if (rt == NULL) {
        return NULL;
    }

    qemu_mutex_lock(&rt->lock);
    tb = q_tree_lookup(rt->tree, &s);
    qemu_mutex_unlock(&rt->lock);
    return tb;
}

static void tcg_region_tree_lock_all(void)
{
    size_t i;

    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_lock(&rt->lock);
    }
}

static void tcg_region_tree_unlock_all(void)
{
    size_t i;

    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_unlock(&rt->lock);
    }
}

void tcg_tb_foreach(GTraverseFunc func, gpointer user_data)
{
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        q_tree_foreach(rt->tree, func, user_data);
    }
    tcg_region_tree_unlock_all();
}

size_t tcg_nb_tbs(void)
{
    size_t nb_tbs = 0;
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        nb_tbs += q_tree_nnodes(rt->tree);
    }
    tcg_region_tree_unlock_all();
    return nb_tbs;
}

static void tcg_region_tree_reset_all(void)
{
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        q_tree_ref(rt->tree);
        q_tree_destroy(rt->tree);
    }
    tcg_region_tree_unlock_all();
}

static void tcg_region_bounds(size_t curr_region, void **pstart, void **pend)
{
    void *start, *end;

    start = region.start_aligned + curr_region * region.stride;
    end = start + region.size;

    if (curr_region == 0) {
        start = region.after_prologue;
    }
    if (curr_region == region.n - 1) {
        end = region.start_aligned + region.total_size;
    }

    *pstart = start;
    *pend = end;
}

static void tcg_region_assign(TCGContext *s, size_t curr_region)
{
    void *start, *end;

    tcg_region_bounds(curr_region, &start, &end);

    s->code_gen_buffer = start;
    s->code_gen_ptr = start;
    s->code_gen_buffer_size = end - start;
    s->code_gen_highwater = end - TCG_HIGHWATER;
}

static bool tcg_region_alloc__locked(TCGContext *s)
{
    if (region.current == region.n) {
        return true;
    }
    tcg_region_assign(s, region.current);
    region.current++;
    return false;
}

bool tcg_region_alloc(TCGContext *s)
{
    bool err;
    size_t size_full = s->code_gen_buffer_size;

    qemu_mutex_lock(&region.lock);
    err = tcg_region_alloc__locked(s);
    if (!err) {
        region.agg_size_full += size_full - TCG_HIGHWATER;
    }
    qemu_mutex_unlock(&region.lock);
    return err;
}

static void tcg_region_initial_alloc__locked(TCGContext *s)
{
    bool err = tcg_region_alloc__locked(s);
    g_assert(!err);
}

void tcg_region_initial_alloc(TCGContext *s)
{
    qemu_mutex_lock(&region.lock);
    tcg_region_initial_alloc__locked(s);
    qemu_mutex_unlock(&region.lock);
}

void tcg_region_reset_all(void)
{
    unsigned int n_ctxs = qatomic_read(&tcg_cur_ctxs);
    unsigned int i;

    qemu_mutex_lock(&region.lock);
    region.current = 0;
    region.agg_size_full = 0;

    for (i = 0; i < n_ctxs; i++) {
        TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        tcg_region_initial_alloc__locked(s);
    }
    qemu_mutex_unlock(&region.lock);

    tcg_region_tree_reset_all();
}

static size_t tcg_n_regions(size_t tb_size, unsigned max_threads)
{
#ifdef CONFIG_USER_ONLY
    return 1;
#else
    size_t n_regions;

    if (max_threads == 1) {
        return 1;
    }

    n_regions = tb_size / (2 * MiB);
    if (n_regions <= max_threads) {
        return max_threads;
    }
    return MIN(n_regions, max_threads * 8);
#endif
}

#define MIN_CODE_GEN_BUFFER_SIZE     (1 * MiB)

#if TCG_TARGET_REG_BITS == 32
#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (32 * MiB)
#ifdef CONFIG_USER_ONLY
#define USE_STATIC_CODE_GEN_BUFFER
#endif
#else
#ifdef CONFIG_USER_ONLY
#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (128 * MiB)
#else
#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (1 * GiB)
#endif
#endif

#define DEFAULT_CODE_GEN_BUFFER_SIZE \
  (DEFAULT_CODE_GEN_BUFFER_SIZE_1 < MAX_CODE_GEN_BUFFER_SIZE \
   ? DEFAULT_CODE_GEN_BUFFER_SIZE_1 : MAX_CODE_GEN_BUFFER_SIZE)

#ifdef USE_STATIC_CODE_GEN_BUFFER
static uint8_t static_code_gen_buffer[DEFAULT_CODE_GEN_BUFFER_SIZE]
    __attribute__((aligned(CODE_GEN_ALIGN)));

static int alloc_code_gen_buffer(size_t tb_size, int splitwx, Error **errp)
{
    void *buf, *end;
    size_t size;

    if (splitwx > 0) {
        error_setg(errp, "jit split-wx not supported");
        return -1;
    }

    buf = static_code_gen_buffer;
    end = static_code_gen_buffer + sizeof(static_code_gen_buffer);
    buf = QEMU_ALIGN_PTR_UP(buf, qemu_real_host_page_size());
    end = QEMU_ALIGN_PTR_DOWN(end, qemu_real_host_page_size());

    size = end - buf;

    if (size > tb_size) {
        size = QEMU_ALIGN_DOWN(tb_size, qemu_real_host_page_size());
    }

    region.start_aligned = buf;
    region.total_size = size;

    return PROT_READ | PROT_WRITE;
}
#elif defined(_WIN32)
static int alloc_code_gen_buffer(size_t size, int splitwx, Error **errp)
{
    void *buf;

    if (splitwx > 0) {
        error_setg(errp, "jit split-wx not supported");
        return -1;
    }

    buf = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                             PAGE_EXECUTE_READWRITE);
    if (buf == NULL) {
        error_setg_win32(errp, GetLastError(),
                         "allocate %zu bytes for jit buffer", size);
        return false;
    }

    region.start_aligned = buf;
    region.total_size = size;

    return PROT_READ | PROT_WRITE | PROT_EXEC;
}
#else
static int alloc_code_gen_buffer_anon(size_t size, int prot,
                                      int flags, Error **errp)
{
    void *buf;

    buf = mmap(NULL, size, prot, flags, -1, 0);
    if (buf == MAP_FAILED) {
        error_setg_errno(errp, errno,
                         "allocate %zu bytes for jit buffer", size);
        return -1;
    }

    region.start_aligned = buf;
    region.total_size = size;
    return prot;
}

#ifndef CONFIG_TCG_INTERPRETER
#ifdef CONFIG_POSIX
#include "qemu/memfd.h"

static int alloc_code_gen_buffer_splitwx_memfd(size_t size, Error **errp)
{
    void *buf_rw = NULL, *buf_rx = MAP_FAILED;
    int fd = -1;

    buf_rw = qemu_memfd_alloc("tcg-jit", size, 0, &fd, errp);
    if (buf_rw == NULL) {
        goto fail;
    }

    buf_rx = mmap(NULL, size, host_prot_read_exec(), MAP_SHARED, fd, 0);
    if (buf_rx == MAP_FAILED) {
        error_setg_errno(errp, errno,
                         "failed to map shared memory for execute");
        goto fail;
    }

    close(fd);
    region.start_aligned = buf_rw;
    region.total_size = size;
    tcg_splitwx_diff = buf_rx - buf_rw;

    return PROT_READ | PROT_WRITE;

 fail:
    if (buf_rw) {
        munmap(buf_rw, size);
    }
    if (fd >= 0) {
        close(fd);
    }
    return -1;
}
#endif

#ifdef CONFIG_DARWIN
#include <mach/mach.h>

extern kern_return_t mach_vm_remap(vm_map_t target_task,
                                   mach_vm_address_t *target_address,
                                   mach_vm_size_t size,
                                   mach_vm_offset_t mask,
                                   int flags,
                                   vm_map_t src_task,
                                   mach_vm_address_t src_address,
                                   boolean_t copy,
                                   vm_prot_t *cur_protection,
                                   vm_prot_t *max_protection,
                                   vm_inherit_t inheritance);

static int alloc_code_gen_buffer_splitwx_vmremap(size_t size, Error **errp)
{
    kern_return_t ret;
    mach_vm_address_t buf_rw, buf_rx;
    vm_prot_t cur_prot, max_prot;

    if (!alloc_code_gen_buffer_anon(size, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, errp)) {
        return -1;
    }

    buf_rw = (mach_vm_address_t)region.start_aligned;
    buf_rx = 0;
    ret = mach_vm_remap(mach_task_self(),
                        &buf_rx,
                        size,
                        0,
                        VM_FLAGS_ANYWHERE,
                        mach_task_self(),
                        buf_rw,
                        false,
                        &cur_prot,
                        &max_prot,
                        VM_INHERIT_NONE);
    if (ret != KERN_SUCCESS) {
        error_setg(errp, "vm_remap for jit splitwx failed");
        munmap((void *)buf_rw, size);
        return -1;
    }

    if (mprotect((void *)buf_rx, size, host_prot_read_exec()) != 0) {
        error_setg_errno(errp, errno, "mprotect for jit splitwx");
        munmap((void *)buf_rx, size);
        munmap((void *)buf_rw, size);
        return -1;
    }

    tcg_splitwx_diff = buf_rx - buf_rw;
    return PROT_READ | PROT_WRITE;
}
#endif
#endif

static int alloc_code_gen_buffer_splitwx(size_t size, Error **errp)
{
#ifndef CONFIG_TCG_INTERPRETER
# ifdef CONFIG_DARWIN
    return alloc_code_gen_buffer_splitwx_vmremap(size, errp);
# endif
# ifdef CONFIG_POSIX
    return alloc_code_gen_buffer_splitwx_memfd(size, errp);
# endif
#endif
    error_setg(errp, "jit split-wx not supported");
    return -1;
}

static int alloc_code_gen_buffer(size_t size, int splitwx, Error **errp)
{
    ERRP_GUARD();
    int prot, flags;

    if (splitwx) {
        prot = alloc_code_gen_buffer_splitwx(size, errp);
        if (prot >= 0) {
            return prot;
        }
        if (splitwx > 0) {
            return -1;
        }
        error_free_or_abort(errp);
    }

    prot = PROT_NONE;
    flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef CONFIG_DARWIN
    if (!splitwx) {
        flags |= MAP_JIT;
    }
#endif

    return alloc_code_gen_buffer_anon(size, prot, flags, errp);
}
#endif

void tcg_region_init(size_t tb_size, int splitwx, unsigned max_threads)
{
    const size_t page_size = qemu_real_host_page_size();
    size_t region_size;
    int have_prot, need_prot;

    if (tb_size == 0) {
        size_t phys_mem = qemu_get_host_physmem();
        if (phys_mem == 0) {
            tb_size = DEFAULT_CODE_GEN_BUFFER_SIZE;
        } else {
            tb_size = QEMU_ALIGN_DOWN(phys_mem / 8, page_size);
            tb_size = MIN(DEFAULT_CODE_GEN_BUFFER_SIZE, tb_size);
        }
    }
    if (tb_size < MIN_CODE_GEN_BUFFER_SIZE) {
        tb_size = MIN_CODE_GEN_BUFFER_SIZE;
    }
    if (tb_size > MAX_CODE_GEN_BUFFER_SIZE) {
        tb_size = MAX_CODE_GEN_BUFFER_SIZE;
    }

    have_prot = alloc_code_gen_buffer(tb_size, splitwx, &error_fatal);
    assert(have_prot >= 0);

    qemu_madvise(region.start_aligned, region.total_size, QEMU_MADV_HUGEPAGE);
    if (tcg_splitwx_diff) {
        qemu_madvise(region.start_aligned + tcg_splitwx_diff,
                     region.total_size, QEMU_MADV_HUGEPAGE);
    }

    region.n = tcg_n_regions(tb_size, max_threads);
    region_size = tb_size / region.n;
    region_size = QEMU_ALIGN_DOWN(region_size, page_size);

    g_assert(region_size >= 2 * page_size);
    region.stride = region_size;

    region.size = region_size - page_size;
    region.total_size -= page_size;

    region.after_prologue = region.start_aligned;

    qemu_mutex_init(&region.lock);

    need_prot = PROT_READ | PROT_WRITE;
#ifndef CONFIG_TCG_INTERPRETER
    if (tcg_splitwx_diff == 0) {
        need_prot |= host_prot_read_exec();
    }
#endif
    for (size_t i = 0, n = region.n; i < n; i++) {
        void *start, *end;

        tcg_region_bounds(i, &start, &end);
        if (have_prot != need_prot) {
            int rc;

            if (need_prot == (PROT_READ | PROT_WRITE | PROT_EXEC)) {
                rc = qemu_mprotect_rwx(start, end - start);
            } else if (need_prot == (PROT_READ | PROT_WRITE)) {
                rc = qemu_mprotect_rw(start, end - start);
            } else {
#ifdef CONFIG_POSIX
                rc = mprotect(start, end - start, need_prot);
                if (rc) {
                    error_report("mprotect of jit buffer: %s",
                                 strerror(errno));
                }
#else
                g_assert_not_reached();
#endif
            }
            if (rc) {
                exit(1);
            }
        }
        if (have_prot != 0) {
            (void)qemu_mprotect_none(end, page_size);
        }
    }

    tcg_region_trees_init();

    tcg_region_initial_alloc__locked(&tcg_init_ctx);
}

void tcg_region_prologue_set(TCGContext *s)
{
    g_assert(region.start_aligned == s->code_gen_buffer);
    region.after_prologue = s->code_ptr;

    tcg_region_assign(s, 0);

    tcg_register_jit(tcg_splitwx_to_rx(region.after_prologue),
                     region.start_aligned + region.total_size -
                     region.after_prologue);
}

size_t tcg_code_size(void)
{
    unsigned int n_ctxs = qatomic_read(&tcg_cur_ctxs);
    unsigned int i;
    size_t total;

    qemu_mutex_lock(&region.lock);
    total = region.agg_size_full;
    for (i = 0; i < n_ctxs; i++) {
        const TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        size_t size;

        size = qatomic_read(&s->code_gen_ptr) - s->code_gen_buffer;
        g_assert(size <= s->code_gen_buffer_size);
        total += size;
    }
    qemu_mutex_unlock(&region.lock);
    return total;
}

size_t tcg_code_capacity(void)
{
    size_t guard_size, capacity;

    guard_size = region.stride - region.size;
    capacity = region.total_size;
    capacity -= (region.n - 1) * guard_size;
    capacity -= region.n * TCG_HIGHWATER;

    return capacity;
}
