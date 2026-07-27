/*
 *  User emulator execution
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "accel/tcg/cpu-ops.h"
#include "disas/disas.h"
#include "exec/vaddr.h"
#include "exec/tlb-flags.h"
#include "tcg/tcg.h"
#include "qemu/bitops.h"
#include "qemu/rcu.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/helper-retaddr.h"
#include "accel/tcg/probe.h"
#include "user/cpu_loop.h"
#include "user/guest-host.h"
#include "qemu/main-loop.h"
#include "user/page-protection.h"
#include "exec/page-protection.h"
#include "exec/helper-proto-common.h"
#include "qemu/atomic128.h"
#include "qemu/bswap.h"
#include "qemu/int128.h"
#include "trace.h"
#include "tcg/tcg-ldst.h"
#include "tcg-accel-ops.h"
#include "backend-ldst.h"
#include "internal-common.h"
#include "tb-internal.h"

__thread uintptr_t helper_retaddr;

void qemu_cpu_kick(CPUState *cpu)
{
    tcg_kick_vcpu_thread(cpu);
}

void qemu_process_cpu_events(CPUState *cpu)
{
    qatomic_set(&cpu->exit_request, false);
    process_queued_cpu_work(cpu);
}

MMUAccessType adjust_signal_pc(uintptr_t *pc, bool is_write)
{
    switch (helper_retaddr) {
    default:
        *pc = helper_retaddr;
        break;

    case 0:
        /*
         * Fault during host memory operation within generated code.
         * (Or, a unrelated bug within qemu, but we can't tell from here).
         *
         * We take the host pc from the signal frame.  However, we cannot
         * use that value directly.  Within cpu_restore_state_from_tb, we
         * assume PC comes from GETPC(), as used by the helper functions,
         * so we adjust the address by -GETPC_ADJ to form an address that
         * is within the call insn, so that the address does not accidentally
         * match the beginning of the next guest insn.  However, when the
         * pc comes from the signal frame it points to the actual faulting
         * host memory insn and not the return from a call insn.
         *
         * Therefore, adjust to compensate for what will be done later
         * by cpu_restore_state_from_tb.
         */
        *pc += GETPC_ADJ;
        break;

    case 1:
        *pc = 0;
        return MMU_INST_FETCH;
    }

    return is_write ? MMU_DATA_STORE : MMU_DATA_LOAD;
}

bool handle_sigsegv_accerr_write(CPUState *cpu, sigset_t *old_set,
                                 uintptr_t host_pc, vaddr guest_addr)
{
    switch (page_unprotect(cpu, guest_addr, host_pc)) {
    case 0:
        return false;
    case 1:
        return true;
    case 2:
        sigprocmask(SIG_SETMASK, old_set, NULL);
        cpu_loop_exit_noexc(cpu);
    default:
        g_assert_not_reached();
    }
}

typedef struct PageFlagsNode {
    struct rcu_head rcu;
    IntervalTreeNode itree;
    int flags;
} PageFlagsNode;

static IntervalTreeRoot pageflags_root;

static PageFlagsNode *pageflags_find(vaddr start, vaddr last)
{
    IntervalTreeNode *n;

    n = interval_tree_iter_first(&pageflags_root, start, last);
    return n ? container_of(n, PageFlagsNode, itree) : NULL;
}

static PageFlagsNode *pageflags_next(PageFlagsNode *p, vaddr start, vaddr last)
{
    IntervalTreeNode *n;

    n = interval_tree_iter_next(&p->itree, start, last);
    return n ? container_of(n, PageFlagsNode, itree) : NULL;
}

int walk_memory_regions(void *priv, walk_memory_regions_fn fn)
{
    IntervalTreeNode *n;
    int rc = 0;

    mmap_lock();
    for (n = interval_tree_iter_first(&pageflags_root, 0, -1);
         n != NULL;
         n = interval_tree_iter_next(n, 0, -1)) {
        PageFlagsNode *p = container_of(n, PageFlagsNode, itree);

        rc = fn(priv, n->start, n->last + 1, p->flags);
        if (rc != 0) {
            break;
        }
    }
    mmap_unlock();

    return rc;
}

static int dump_region(void *opaque, vaddr start, vaddr end, int prot)
{
    FILE *f = opaque;
    uint64_t mask;
    int width;

    if (guest_addr_max <= UINT32_MAX) {
        mask = UINT32_MAX, width = 8;
    } else {
        mask = UINT64_MAX, width = 16;
    }

    fprintf(f, "%0*" PRIx64 "-%0*" PRIx64 " %0*" PRIx64 " %c%c%c\n",
            width, start & mask,
            width, end & mask,
            width, (end - start) & mask,
            ((prot & PAGE_READ) ? 'r' : '-'),
            ((prot & PAGE_WRITE) ? 'w' : '-'),
            ((prot & PAGE_EXEC) ? 'x' : '-'));
    return 0;
}

void page_dump(FILE *f)
{
    int width = guest_addr_max <= UINT32_MAX ? 8 : 16;

    fprintf(f, "%-*s %-*s %-*s %s\n",
            width, "start", width, "end", width, "size", "prot");
    walk_memory_regions(f, dump_region);
}

int page_get_flags(vaddr address)
{
    PageFlagsNode *p = pageflags_find(address, address);

    if (p) {
        return p->flags;
    }
    if (have_mmap_lock()) {
        return 0;
    }

    mmap_lock();
    p = pageflags_find(address, address);
    mmap_unlock();
    return p ? p->flags : 0;
}

static void pageflags_create(vaddr start, vaddr last, int flags)
{
    PageFlagsNode *p = g_new(PageFlagsNode, 1);

    p->itree.start = start;
    p->itree.last = last;
    p->flags = flags;
    interval_tree_insert(&p->itree, &pageflags_root);
}

static void pageflags_create_merge(vaddr start, vaddr last, int flags)
{
    PageFlagsNode *next = NULL, *prev = NULL;

    if (start > 0) {
        prev = pageflags_find(start - 1, start - 1);
        if (prev) {
            if (prev->flags == flags) {
                interval_tree_remove(&prev->itree, &pageflags_root);
            } else {
                prev = NULL;
            }
        }
    }
    if (last + 1 != 0) {
        next = pageflags_find(last + 1, last + 1);
        if (next) {
            if (next->flags == flags) {
                interval_tree_remove(&next->itree, &pageflags_root);
            } else {
                next = NULL;
            }
        }
    }

    if (prev) {
        if (next) {
            prev->itree.last = next->itree.last;
            g_free_rcu(next, rcu);
        } else {
            prev->itree.last = last;
        }
        interval_tree_insert(&prev->itree, &pageflags_root);
    } else if (next) {
        next->itree.start = start;
        interval_tree_insert(&next->itree, &pageflags_root);
    } else {
        pageflags_create(start, last, flags);
    }
}

static bool pageflags_set_clear(vaddr start, vaddr last,
                                int set_flags, int clear_flags)
{
    PageFlagsNode *p;
    vaddr p_start, p_last;
    int p_flags, merge_flags;
    bool inval_tb = false;

 restart:
    p = pageflags_find(start, last);
    if (!p) {
        if (set_flags & PAGE_VALID) {
            pageflags_create_merge(start, last, set_flags);
        }
        goto done;
    }

    p_start = p->itree.start;
    p_last = p->itree.last;
    p_flags = p->flags;
    merge_flags = (p_flags & ~clear_flags) | set_flags;

    if ((p_flags & PAGE_EXEC)
        && (!(merge_flags & PAGE_EXEC)
            || (merge_flags & ~p_flags & PAGE_WRITE)
            || (clear_flags & PAGE_VALID))) {
        inval_tb = true;
    }

    if (start == p_start && last == p_last) {
        if (merge_flags & PAGE_VALID) {
            p->flags = merge_flags;
        } else {
            interval_tree_remove(&p->itree, &pageflags_root);
            g_free_rcu(p, rcu);
        }
        goto done;
    }

    if (set_flags != merge_flags) {
        if (p_start < start) {
            interval_tree_remove(&p->itree, &pageflags_root);
            p->itree.last = start - 1;
            interval_tree_insert(&p->itree, &pageflags_root);

            if (last < p_last) {
                if (merge_flags & PAGE_VALID) {
                    pageflags_create(start, last, merge_flags);
                }
                pageflags_create(last + 1, p_last, p_flags);
            } else {
                if (merge_flags & PAGE_VALID) {
                    pageflags_create(start, p_last, merge_flags);
                }
                if (p_last < last) {
                    start = p_last + 1;
                    goto restart;
                }
            }
        } else {
            if (start < p_start && (set_flags & PAGE_VALID)) {
                pageflags_create(start, p_start - 1, set_flags);
            }
            if (last < p_last) {
                interval_tree_remove(&p->itree, &pageflags_root);
                p->itree.start = last + 1;
                interval_tree_insert(&p->itree, &pageflags_root);
                if (merge_flags & PAGE_VALID) {
                    pageflags_create(start, last, merge_flags);
                }
            } else {
                if (merge_flags & PAGE_VALID) {
                    p->flags = merge_flags;
                } else {
                    interval_tree_remove(&p->itree, &pageflags_root);
                    g_free_rcu(p, rcu);
                }
                if (p_last < last) {
                    start = p_last + 1;
                    goto restart;
                }
            }
        }
        goto done;
    }

    if (set_flags == p_flags) {
        if (start < p_start) {
            interval_tree_remove(&p->itree, &pageflags_root);
            p->itree.start = start;
            interval_tree_insert(&p->itree, &pageflags_root);
        }
        if (p_last < last) {
            start = p_last + 1;
            goto restart;
        }
        goto done;
    }

    interval_tree_remove(&p->itree, &pageflags_root);
    if (p_start < start) {
        p->itree.last = start - 1;
        interval_tree_insert(&p->itree, &pageflags_root);

        if (p_last < last) {
            goto restart;
        }
        if (last < p_last) {
            pageflags_create(last + 1, p_last, p_flags);
        }
    } else if (last < p_last) {
        p->itree.start = last + 1;
        interval_tree_insert(&p->itree, &pageflags_root);
    } else {
        g_free_rcu(p, rcu);
        goto restart;
    }
    if (set_flags & PAGE_VALID) {
        pageflags_create(start, last, set_flags);
    }

 done:
    return inval_tb;
}

void page_set_flags(vaddr start, vaddr last, int set_flags, int clear_flags)
{
    assert(start <= last);
    assert(last <= guest_addr_max);
    assert_memory_lock();

    start &= TARGET_PAGE_MASK;
    last |= ~TARGET_PAGE_MASK;

    if (set_flags & PAGE_WRITE) {
        set_flags |= PAGE_WRITE_ORG;
    }
    if (clear_flags & PAGE_WRITE) {
        clear_flags |= PAGE_WRITE_ORG;
    }

    if (clear_flags & PAGE_VALID) {
        page_reset_target_data(start, last);
        clear_flags = -1;
    } else {
        assert(!(set_flags & PAGE_ANON));
    }

    if (pageflags_set_clear(start, last, set_flags, clear_flags)) {
        tb_invalidate_phys_range(NULL, start, last);
    }
}

bool page_check_range(vaddr start, vaddr len, int flags)
{
    vaddr last;
    int locked;
    bool ret;

    if (len == 0) {
        return true;
    }

    last = start + len - 1;
    if (last < start) {
        return false;
    }

    locked = have_mmap_lock();
    while (true) {
        PageFlagsNode *p = pageflags_find(start, last);
        int missing;

        if (!p) {
            if (!locked) {
                mmap_lock();
                locked = -1;
                p = pageflags_find(start, last);
            }
            if (!p) {
                ret = false;
                break;
            }
        }
        if (start < p->itree.start) {
            ret = false;
            break;
        }

        missing = flags & ~p->flags;
        if (missing & ~PAGE_WRITE) {
            ret = false;
            break;
        }
        if (missing & PAGE_WRITE) {
            if (!(p->flags & PAGE_WRITE_ORG)) {
                ret = false;
                break;
            }
            if (!page_unprotect(NULL, start, 0)) {
                ret = false;
                break;
            }
            if (last - start < TARGET_PAGE_SIZE) {
                ret = true;
                break;
            }
            start += TARGET_PAGE_SIZE;
            continue;
        }

        if (last <= p->itree.last) {
            ret = true;
            break;
        }
        start = p->itree.last + 1;
    }

    if (locked < 0) {
        mmap_unlock();
    }
    return ret;
}

bool page_check_range_empty(vaddr start, vaddr last)
{
    assert(last >= start);
    assert_memory_lock();
    return pageflags_find(start, last) == NULL;
}

vaddr page_find_range_empty(vaddr min, vaddr max, vaddr len, vaddr align)
{
    vaddr len_m1, align_m1;

    assert(min <= max);
    assert(max <= guest_addr_max);
    assert(len != 0);
    assert(is_power_of_2(align));
    assert_memory_lock();

    len_m1 = len - 1;
    align_m1 = align - 1;

    while (1) {
        PageFlagsNode *p;

        min = (min + align_m1) & ~align_m1;
        if (min > max) {
            return -1;
        }
        if (len_m1 > max - min) {
            return -1;
        }

        p = pageflags_find(min, min + len_m1);
        if (p == NULL) {
            return min;
        }
        if (max <= p->itree.last) {
            return -1;
        }
        min = p->itree.last + 1;
    }
}

void tb_lock_page0(tb_page_addr_t address)
{
    PageFlagsNode *p;
    vaddr start, last;
    int host_page_size = qemu_real_host_page_size();
    int prot;

    assert_memory_lock();

    if (host_page_size <= TARGET_PAGE_SIZE) {
        start = address & TARGET_PAGE_MASK;
        last = start + TARGET_PAGE_SIZE - 1;
    } else {
        start = address & -host_page_size;
        last = start + host_page_size - 1;
    }

    p = pageflags_find(start, last);
    if (!p) {
        return;
    }
    prot = p->flags;

    if (unlikely(p->itree.last < last)) {
        assert(TARGET_PAGE_SIZE < host_page_size);
        while ((p = pageflags_next(p, start, last)) != NULL) {
            prot |= p->flags;
        }
    }

    if (prot & PAGE_WRITE) {
        pageflags_set_clear(start, last, 0, PAGE_WRITE);
        mprotect(g2h_untagged(start), last - start + 1,
                 prot & (PAGE_READ | PAGE_EXEC) ? PROT_READ : PROT_NONE);
    }
}

int page_unprotect(CPUState *cpu, tb_page_addr_t address, uintptr_t pc)
{
    PageFlagsNode *p;
    bool current_tb_invalidated;

    assert((cpu == NULL) == (pc == 0));

    mmap_lock();

    p = pageflags_find(address, address);

    if (!p || !(p->flags & PAGE_WRITE_ORG)) {
        mmap_unlock();
        return 0;
    }

    current_tb_invalidated = false;
    if (p->flags & PAGE_WRITE) {
        if (pc && cpu->cc->tcg_ops->precise_smc) {
            TranslationBlock *current_tb = tcg_tb_lookup(pc);
            if (current_tb) {
                current_tb_invalidated = tb_cflags(current_tb) & CF_INVALID;
            }
        }
    } else {
        int host_page_size = qemu_real_host_page_size();
        vaddr start, len, i;
        int prot;

        if (host_page_size <= TARGET_PAGE_SIZE) {
            start = address & TARGET_PAGE_MASK;
            len = TARGET_PAGE_SIZE;
            prot = p->flags | PAGE_WRITE;
            pageflags_set_clear(start, start + len - 1, PAGE_WRITE, 0);
            current_tb_invalidated =
                tb_invalidate_phys_page_unwind(cpu, start, pc);
        } else {
            start = address & -host_page_size;
            len = host_page_size;
            prot = 0;

            for (i = 0; i < len; i += TARGET_PAGE_SIZE) {
                vaddr addr = start + i;

                p = pageflags_find(addr, addr);
                if (p) {
                    prot |= p->flags;
                    if (p->flags & PAGE_WRITE_ORG) {
                        prot |= PAGE_WRITE;
                        pageflags_set_clear(addr, addr + TARGET_PAGE_SIZE - 1,
                                            PAGE_WRITE, 0);
                    }
                }
                current_tb_invalidated |=
                    tb_invalidate_phys_page_unwind(cpu, addr, pc);
            }
        }
        if (prot & PAGE_EXEC) {
            prot = (prot & ~PAGE_EXEC) | PAGE_READ;
        }
        mprotect((void *)g2h_untagged(start), len, prot & PAGE_RWX);
    }
    mmap_unlock();

    return current_tb_invalidated ? 2 : 1;
}

static int probe_access_internal(CPUArchState *env, vaddr addr,
                                 int fault_size, MMUAccessType access_type,
                                 bool nonfault, uintptr_t ra)
{
    int acc_flag;
    bool maperr;

    switch (access_type) {
    case MMU_DATA_STORE:
        acc_flag = PAGE_WRITE_ORG;
        break;
    case MMU_DATA_LOAD:
        acc_flag = PAGE_READ;
        break;
    case MMU_INST_FETCH:
        acc_flag = PAGE_EXEC;
        break;
    default:
        g_assert_not_reached();
    }

    if (guest_addr_valid_untagged(addr)) {
        int page_flags = page_get_flags(addr);
        if (page_flags & acc_flag) {
            if (access_type != MMU_INST_FETCH
                && cpu_plugin_mem_cbs_enabled(env_cpu(env))) {
                return TLB_MMIO;
            }
            return 0;
        }
        maperr = !(page_flags & PAGE_VALID);
    } else {
        maperr = true;
    }

    if (nonfault) {
        return TLB_INVALID_MASK;
    }

    cpu_loop_exit_sigsegv(env_cpu(env), addr, access_type, maperr, ra);
}

int probe_access_flags(CPUArchState *env, vaddr addr, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t ra)
{
    int flags;

    g_assert(-(addr | TARGET_PAGE_MASK) >= size);
    flags = probe_access_internal(env, addr, size, access_type, nonfault, ra);
    *phost = (flags & TLB_INVALID_MASK) ? NULL : g2h(env_cpu(env), addr);
    return flags;
}

void *probe_access(CPUArchState *env, vaddr addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t ra)
{
    int flags;

    g_assert(-(addr | TARGET_PAGE_MASK) >= size);
    flags = probe_access_internal(env, addr, size, access_type, false, ra);
    g_assert((flags & ~TLB_MMIO) == 0);

    return size ? g2h(env_cpu(env), addr) : NULL;
}

void *tlb_vaddr_to_host(CPUArchState *env, vaddr addr,
                        MMUAccessType access_type, int mmu_idx)
{
    return g2h(env_cpu(env), addr);
}

tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr addr,
                                        void **hostp)
{
    int flags;

    flags = probe_access_internal(env, addr, 1, MMU_INST_FETCH, false, 0);
    g_assert(flags == 0);

    if (hostp) {
        *hostp = g2h_untagged(addr);
    }
    return addr;
}

#define TPD_PAGES  64
#define TBD_MASK   (TARGET_PAGE_MASK * TPD_PAGES)

typedef struct TargetPageDataNode {
    struct rcu_head rcu;
    IntervalTreeNode itree;
    char data[] __attribute__((aligned));
} TargetPageDataNode;

static IntervalTreeRoot targetdata_root;
static size_t target_page_data_size;

void page_reset_target_data(vaddr start, vaddr last)
{
    IntervalTreeNode *n, *next;
    size_t size = target_page_data_size;

    if (likely(size == 0)) {
        return;
    }

    assert_memory_lock();

    start &= TARGET_PAGE_MASK;
    last |= ~TARGET_PAGE_MASK;

    for (n = interval_tree_iter_first(&targetdata_root, start, last),
         next = n ? interval_tree_iter_next(n, start, last) : NULL;
         n != NULL;
         n = next,
         next = next ? interval_tree_iter_next(n, start, last) : NULL) {
        vaddr n_start, n_last, p_ofs, p_len;
        TargetPageDataNode *t = container_of(n, TargetPageDataNode, itree);

        if (n->start >= start && n->last <= last) {
            interval_tree_remove(n, &targetdata_root);
            g_free_rcu(t, rcu);
            continue;
        }

        if (n->start < start) {
            n_start = start;
            p_ofs = (start - n->start) >> TARGET_PAGE_BITS;
        } else {
            n_start = n->start;
            p_ofs = 0;
        }
        n_last = MIN(last, n->last);
        p_len = (n_last + 1 - n_start) >> TARGET_PAGE_BITS;

        memset(t->data + p_ofs * size, 0, p_len * size);
    }
}

void *page_get_target_data(vaddr address, size_t size)
{
    IntervalTreeNode *n;
    TargetPageDataNode *t;
    vaddr page, region, p_ofs;

    if (unlikely(target_page_data_size != size)) {
        assert(target_page_data_size == 0);
        target_page_data_size = size;
    }

    page = address & TARGET_PAGE_MASK;
    region = address & TBD_MASK;

    n = interval_tree_iter_first(&targetdata_root, page, page);
    if (!n) {
        mmap_lock();
        n = interval_tree_iter_first(&targetdata_root, page, page);
        if (!n) {
            t = g_malloc0(sizeof(TargetPageDataNode) + TPD_PAGES * size);
            n = &t->itree;
            n->start = region;
            n->last = region | ~TBD_MASK;
            interval_tree_insert(n, &targetdata_root);
        }
        mmap_unlock();
    }

    t = container_of(n, TargetPageDataNode, itree);
    p_ofs = (page - region) >> TARGET_PAGE_BITS;
    return t->data + p_ofs * size;
}

static void *cpu_mmu_lookup(CPUState *cpu, vaddr addr,
                            MemOp mop, uintptr_t ra, MMUAccessType type)
{
    int a_bits = memop_alignment_bits(mop);
    void *ret;

    if (unlikely(addr & ((1 << a_bits) - 1))) {
        cpu_loop_exit_sigbus(cpu, addr, type, ra);
    }

    ret = g2h(cpu, addr);
    set_helper_retaddr(ra);
    return ret;
}

int cpu_memory_rw_debug(CPUState *cpu, vaddr addr,
                        void *ptr, size_t len, bool is_write)
{
    int flags;
    vaddr l, page;
    uint8_t *buf = ptr;
    ssize_t written;
    int ret = -1;
    int fd = -1;

    mmap_lock();

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len) {
            l = len;
        }
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID)) {
            goto out_close;
        }
        if (is_write) {
            if (flags & PAGE_WRITE) {
                memcpy(g2h(cpu, addr), buf, l);
            } else {
                if (fd == -1) {
                    fd = open("/proc/self/mem", O_WRONLY);
                    if (fd == -1) {
                        goto out;
                    }
                }
                tb_invalidate_phys_range(NULL, addr, addr + l - 1);
                written = pwrite(fd, buf, l,
                                 (off_t)(uintptr_t)g2h_untagged(addr));
                if (written != l) {
                    goto out_close;
                }
            }
        } else if (flags & PAGE_READ) {
            memcpy(buf, g2h(cpu, addr), l);
        } else {
            if (fd == -1) {
                fd = open("/proc/self/mem", O_RDONLY);
                if (fd == -1) {
                    goto out;
                }
            }
            if (pread(fd, buf, l,
                      (off_t)(uintptr_t)g2h_untagged(addr)) != l) {
                goto out_close;
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
    ret = 0;
out_close:
    if (fd != -1) {
        close(fd);
    }
out:
    mmap_unlock();

    return ret;
}

#include "ldst_atomicity.c.inc"

static uint8_t do_ld1_mmu(CPUState *cpu, vaddr addr, MemOpIdx oi,
                          uintptr_t ra, MMUAccessType access_type)
{
    void *haddr;
    uint8_t ret;

    cpu_req_mo(cpu, TCG_MO_LD_LD | TCG_MO_ST_LD);
    haddr = cpu_mmu_lookup(cpu, addr, get_memop(oi), ra, access_type);
    ret = ldub_p(haddr);
    clear_helper_retaddr();
    return ret;
}

static uint16_t do_ld2_mmu(CPUState *cpu, vaddr addr, MemOpIdx oi,
                           uintptr_t ra, MMUAccessType access_type)
{
    void *haddr;
    uint16_t ret;
    MemOp mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_LD | TCG_MO_ST_LD);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, access_type);
    ret = load_atom_2(cpu, ra, haddr, mop);
    clear_helper_retaddr();

    if (mop & MO_BSWAP) {
        ret = bswap16(ret);
    }
    return ret;
}

static uint32_t do_ld4_mmu(CPUState *cpu, vaddr addr, MemOpIdx oi,
                           uintptr_t ra, MMUAccessType access_type)
{
    void *haddr;
    uint32_t ret;
    MemOp mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_LD | TCG_MO_ST_LD);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, access_type);
    ret = load_atom_4(cpu, ra, haddr, mop);
    clear_helper_retaddr();

    if (mop & MO_BSWAP) {
        ret = bswap32(ret);
    }
    return ret;
}

static uint64_t do_ld8_mmu(CPUState *cpu, vaddr addr, MemOpIdx oi,
                           uintptr_t ra, MMUAccessType access_type)
{
    void *haddr;
    uint64_t ret;
    MemOp mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_LD | TCG_MO_ST_LD);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, access_type);
    ret = load_atom_8(cpu, ra, haddr, mop);
    clear_helper_retaddr();

    if (mop & MO_BSWAP) {
        ret = bswap64(ret);
    }
    return ret;
}

static Int128 do_ld16_mmu(CPUState *cpu, vaddr addr,
                          MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    Int128 ret;
    MemOp mop = get_memop(oi);

    tcg_debug_assert((mop & MO_SIZE) == MO_128);
    cpu_req_mo(cpu, TCG_MO_LD_LD | TCG_MO_ST_LD);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, MMU_DATA_LOAD);
    ret = load_atom_16(cpu, ra, haddr, mop);
    clear_helper_retaddr();

    if (mop & MO_BSWAP) {
        ret = bswap128(ret);
    }
    return ret;
}

static void do_st1_mmu(CPUState *cpu, vaddr addr, uint8_t val,
                       MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    cpu_req_mo(cpu, TCG_MO_LD_ST | TCG_MO_ST_ST);
    haddr = cpu_mmu_lookup(cpu, addr, get_memop(oi), ra, MMU_DATA_STORE);
    stb_p(haddr, val);
    clear_helper_retaddr();
}

static void do_st2_mmu(CPUState *cpu, vaddr addr, uint16_t val,
                       MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    MemOp mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_ST | TCG_MO_ST_ST);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, MMU_DATA_STORE);

    if (mop & MO_BSWAP) {
        val = bswap16(val);
    }
    store_atom_2(cpu, ra, haddr, mop, val);
    clear_helper_retaddr();
}

static void do_st4_mmu(CPUState *cpu, vaddr addr, uint32_t val,
                       MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    MemOp mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_ST | TCG_MO_ST_ST);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, MMU_DATA_STORE);

    if (mop & MO_BSWAP) {
        val = bswap32(val);
    }
    store_atom_4(cpu, ra, haddr, mop, val);
    clear_helper_retaddr();
}

static void do_st8_mmu(CPUState *cpu, vaddr addr, uint64_t val,
                       MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    MemOp mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_ST | TCG_MO_ST_ST);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, MMU_DATA_STORE);

    if (mop & MO_BSWAP) {
        val = bswap64(val);
    }
    store_atom_8(cpu, ra, haddr, mop, val);
    clear_helper_retaddr();
}

static void do_st16_mmu(CPUState *cpu, vaddr addr, Int128 val,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    MemOpIdx mop = get_memop(oi);

    cpu_req_mo(cpu, TCG_MO_LD_ST | TCG_MO_ST_ST);
    haddr = cpu_mmu_lookup(cpu, addr, mop, ra, MMU_DATA_STORE);

    if (mop & MO_BSWAP) {
        val = bswap128(val);
    }
    store_atom_16(cpu, ra, haddr, mop, val);
    clear_helper_retaddr();
}

uint8_t cpu_ldb_code_mmu(CPUArchState *env, vaddr addr,
                         MemOpIdx oi, uintptr_t ra)
{
    return do_ld1_mmu(env_cpu(env), addr, oi, ra ? ra : 1, MMU_INST_FETCH);
}

uint16_t cpu_ldw_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra)
{
    return do_ld2_mmu(env_cpu(env), addr, oi, ra ? ra : 1, MMU_INST_FETCH);
}

uint32_t cpu_ldl_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra)
{
    return do_ld4_mmu(env_cpu(env), addr, oi, ra ? ra : 1, MMU_INST_FETCH);
}

uint64_t cpu_ldq_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra)
{
    return do_ld8_mmu(env_cpu(env), addr, oi, ra ? ra : 1, MMU_INST_FETCH);
}

#include "ldst_common.c.inc"

static void *atomic_mmu_lookup(CPUState *cpu, vaddr addr, MemOpIdx oi,
                               int size, uintptr_t retaddr)
{
    MemOp mop = get_memop(oi);
    int a_bits = memop_alignment_bits(mop);
    void *ret;

    if (unlikely(addr & ((1 << a_bits) - 1))) {
        cpu_loop_exit_sigbus(cpu, addr, MMU_DATA_STORE, retaddr);
    }

    if (unlikely(addr & (size - 1))) {
        cpu_loop_exit_atomic(cpu, retaddr);
    }

    ret = g2h(cpu, addr);
    set_helper_retaddr(retaddr);
    return ret;
}

#include "atomic_common.c.inc"

#define ATOMIC_NAME(X) \
    glue(glue(glue(cpu_atomic_ ## X, SUFFIX), END), _mmu)
#define ATOMIC_MMU_CLEANUP do { clear_helper_retaddr(); } while (0)

#define DATA_SIZE 1
#include "atomic_template.h"

#define DATA_SIZE 2
#include "atomic_template.h"

#define DATA_SIZE 4
#include "atomic_template.h"

#ifdef CONFIG_ATOMIC64
#define DATA_SIZE 8
#include "atomic_template.h"
#endif

#if defined(CONFIG_ATOMIC128) || HAVE_CMPXCHG128
#define DATA_SIZE 16
#include "atomic_template.h"
#endif
