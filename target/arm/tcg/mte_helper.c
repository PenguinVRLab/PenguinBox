/*
 * ARM v8.5-MemTag Operations
 *
 * Copyright (c) 2020 Linaro, Ltd.
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
#include "qemu/log.h"
#include "cpu.h"
#include "internals.h"
#include "exec/target_page.h"
#include "exec/page-protection.h"
#ifdef CONFIG_USER_ONLY
#include "user/cpu_loop.h"
#include "user/page-protection.h"
#else
#include "system/physmem.h"
#endif
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/helper-proto.h"
#include "exec/tlb-flags.h"
#include "accel/tcg/cpu-ops.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"
#include "mte_helper.h"

static int choose_nonexcluded_tag(int tag, int offset, uint16_t exclude)
{
    if (exclude == 0xffff) {
        return 0;
    }
    if (offset == 0) {
        while (exclude & (1 << tag)) {
            tag = (tag + 1) & 15;
        }
    } else {
        do {
            do {
                tag = (tag + 1) & 15;
            } while (exclude & (1 << tag));
        } while (--offset > 0);
    }
    return tag;
}

uint8_t *allocation_tag_mem_probe(CPUARMState *env, int ptr_mmu_idx,
                                  uint64_t ptr, MMUAccessType ptr_access,
                                  int ptr_size, MMUAccessType tag_access,
                                  bool probe, uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    const size_t page_data_size = TARGET_PAGE_SIZE >> (LOG2_TAG_GRANULE + 1);
    uint64_t clean_ptr = useronly_clean_ptr(ptr);
    int flags = page_get_flags(clean_ptr);
    uint8_t *tags;
    uintptr_t index;

    assert(!(probe && ra));

    if (!(flags & (ptr_access == MMU_DATA_STORE ? PAGE_WRITE_ORG : PAGE_READ))) {
        if (probe) {
            return NULL;
        }
        cpu_loop_exit_sigsegv(env_cpu(env), ptr, ptr_access,
                              !(flags & PAGE_VALID), ra);
    }

    if (!(flags & PAGE_ANON) || !(flags & PAGE_MTE)) {
        return NULL;
    }

    tags = page_get_target_data(clean_ptr, page_data_size);

    index = extract32(ptr, LOG2_TAG_GRANULE + 1,
                      TARGET_PAGE_BITS - LOG2_TAG_GRANULE - 1);
    return tags + index;
#else
    CPUTLBEntryFull *full;
    MemTxAttrs attrs;
    int in_page, flags;
    hwaddr ptr_paddr, tag_paddr, xlat;
    MemoryRegion *mr;
    ARMASIdx tag_asi;
    AddressSpace *tag_as;
    void *host;

    flags = probe_access_full(env, ptr, 0, ptr_access, ptr_mmu_idx,
                              ra == 0, &host, &full, ra);
    if (probe && (flags & TLB_INVALID_MASK)) {
        return NULL;
    }
    assert(!(flags & TLB_INVALID_MASK));

    if (full->extra.arm.pte_attrs != 0xf0) {
        return NULL;
    }

    if (unlikely(flags & TLB_MMIO)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Page @ 0x%" PRIx64 " indicates Tagged Normal memory "
                      "but is not backed by host ram\n", ptr);
        return NULL;
    }

    ptr_paddr = full->phys_addr | (ptr & ~TARGET_PAGE_MASK);
    attrs = full->attrs;
    full = NULL;

    in_page = -(ptr | TARGET_PAGE_MASK);
    if (unlikely(ptr_size > in_page)) {
        flags |= probe_access_full(env, ptr + in_page, 0, ptr_access,
                                   ptr_mmu_idx, ra == 0, &host, &full, ra);
        assert(!(flags & TLB_INVALID_MASK));
    }

    if (!probe && unlikely(flags & TLB_WATCHPOINT)) {
        int wp = ptr_access == MMU_DATA_LOAD ? BP_MEM_READ : BP_MEM_WRITE;
        assert(ra != 0);
        cpu_check_watchpoint(env_cpu(env), ptr, ptr_size, attrs, wp, ra);
    }

    tag_paddr = ptr_paddr >> (LOG2_TAG_GRANULE + 1);

    tag_asi = attrs.secure ? ARMASIdx_TagS : ARMASIdx_TagNS;
    tag_as = cpu_get_address_space(env_cpu(env), tag_asi);
    mr = address_space_translate(tag_as, tag_paddr, &xlat, NULL,
                                 tag_access == MMU_DATA_STORE, attrs);

    if (unlikely(!memory_region_is_ram(mr))) {
        qemu_log_mask(LOG_UNIMP,
                      "Tag Memory @ 0x%" HWADDR_PRIx " not found for "
                      "Normal Memory @ 0x%" HWADDR_PRIx "\n",
                      tag_paddr, ptr_paddr);
        return NULL;
    }

    if (tag_access == MMU_DATA_STORE) {
        ram_addr_t tag_ra = memory_region_get_ram_addr(mr) + xlat;
        physical_memory_set_dirty_flag(tag_ra, DIRTY_MEMORY_MIGRATION);
    }

    return memory_region_get_ram_ptr(mr) + xlat;
#endif
}

static uint8_t *allocation_tag_mem(CPUARMState *env, int ptr_mmu_idx,
                                   uint64_t ptr, MMUAccessType ptr_access,
                                   int ptr_size, MMUAccessType tag_access,
                                   uintptr_t ra)
{
    return allocation_tag_mem_probe(env, ptr_mmu_idx, ptr, ptr_access,
                                    ptr_size, tag_access, false, ra);
}

uint64_t HELPER(irg)(CPUARMState *env, uint64_t rn, uint64_t rm)
{
    uint16_t exclude = extract32(rm | env->cp15.gcr_el1, 0, 16);
    int rrnd = extract32(env->cp15.gcr_el1, 16, 1);
    int start = extract32(env->cp15.rgsr_el1, 0, 4);
    int seed = extract32(env->cp15.rgsr_el1, 8, 16);
    int offset, i, rtag;

    if (unlikely(seed == 0) && rrnd) {
        do {
            Error *err = NULL;
            uint16_t two;

            if (qemu_guest_getrandom(&two, sizeof(two), &err) < 0) {
                qemu_log_mask(LOG_UNIMP, "IRG: Crypto failure: %s\n",
                              error_get_pretty(err));
                error_free(err);
                two = 1;
            }
            seed = two;
        } while (seed == 0);
    }

    for (i = offset = 0; i < 4; ++i) {
        int top = (extract32(seed, 5, 1) ^ extract32(seed, 3, 1) ^
                   extract32(seed, 2, 1) ^ extract32(seed, 0, 1));
        seed = (top << 15) | (seed >> 1);
        offset |= top << i;
    }
    rtag = choose_nonexcluded_tag(start, offset, exclude);
    env->cp15.rgsr_el1 = rtag | (seed << 8);

    return address_with_allocation_tag(rn, rtag);
}

uint64_t HELPER(addsubg)(CPUARMState *env, uint64_t ptr,
                         int32_t offset, uint32_t tag_offset)
{
    int start_tag = allocation_tag_from_addr(ptr);
    uint16_t exclude = extract32(env->cp15.gcr_el1, 0, 16);
    int rtag = choose_nonexcluded_tag(start_tag, tag_offset, exclude);

    return address_with_allocation_tag(ptr + offset, rtag);
}

int load_tag1(uint64_t ptr, uint8_t *mem)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    return extract32(*mem, ofs, 4);
}

uint64_t HELPER(ldg)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    int mmu_idx = arm_env_mmu_index(env);
    uint8_t *mem;
    int rtag = 0;

    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_LOAD, 1,
                             MMU_DATA_LOAD, GETPC());

    if (mem) {
        rtag = load_tag1(ptr, mem);
    }

    return address_with_allocation_tag(xt, rtag);
}

static void check_tag_aligned(CPUARMState *env, uint64_t ptr, uintptr_t ra)
{
    if (unlikely(!QEMU_IS_ALIGNED(ptr, TAG_GRANULE))) {
        arm_cpu_do_unaligned_access(env_cpu(env), ptr, MMU_DATA_STORE,
                                    arm_env_mmu_index(env), ra);
        g_assert_not_reached();
    }
}

void store_tag1(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    *mem = deposit32(*mem, ofs, 4, tag);
}

static void store_tag1_parallel(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    uint8_t old = qatomic_read(mem);

    while (1) {
        uint8_t new = deposit32(old, ofs, 4, tag);
        uint8_t cmp = qatomic_cmpxchg(mem, old, new);
        if (likely(cmp == old)) {
            return;
        }
        old = cmp;
    }
}

typedef void stg_store1(uint64_t, uint8_t *, int);

static inline void do_stg(CPUARMState *env, uint64_t ptr, uint64_t xt,
                          uintptr_t ra, stg_store1 store1)
{
    int mmu_idx = arm_env_mmu_index(env);
    uint8_t *mem;

    check_tag_aligned(env, ptr, ra);

    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE, TAG_GRANULE,
                             MMU_DATA_STORE, ra);

    if (mem) {
        store1(ptr, mem, allocation_tag_from_addr(xt));
    }
}

void HELPER(stg)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_stg(env, ptr, xt, GETPC(), store_tag1);
}

void HELPER(stg_parallel)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_stg(env, ptr, xt, GETPC(), store_tag1_parallel);
}

void HELPER(stg_stub)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();

    check_tag_aligned(env, ptr, ra);
    probe_write(env, ptr, TAG_GRANULE, mmu_idx, ra);
}

static inline void do_st2g(CPUARMState *env, uint64_t ptr, uint64_t xt,
                           uintptr_t ra, stg_store1 store1)
{
    int mmu_idx = arm_env_mmu_index(env);
    int tag = allocation_tag_from_addr(xt);
    uint8_t *mem1, *mem2;

    check_tag_aligned(env, ptr, ra);

    if (ptr & TAG_GRANULE) {
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                  TAG_GRANULE, MMU_DATA_STORE, ra);
        mem2 = allocation_tag_mem(env, mmu_idx, ptr + TAG_GRANULE,
                                  MMU_DATA_STORE, TAG_GRANULE,
                                  MMU_DATA_STORE, ra);

        if (mem1) {
            store1(TAG_GRANULE, mem1, tag);
        }
        if (mem2) {
            store1(0, mem2, tag);
        }
    } else {
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                  2 * TAG_GRANULE, MMU_DATA_STORE, ra);
        if (mem1) {
            tag |= tag << 4;
            qatomic_set(mem1, tag);
        }
    }
}

void HELPER(st2g)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_st2g(env, ptr, xt, GETPC(), store_tag1);
}

void HELPER(st2g_parallel)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_st2g(env, ptr, xt, GETPC(), store_tag1_parallel);
}

void HELPER(st2g_stub)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();
    int in_page = -(ptr | TARGET_PAGE_MASK);

    check_tag_aligned(env, ptr, ra);

    if (likely(in_page >= 2 * TAG_GRANULE)) {
        probe_write(env, ptr, 2 * TAG_GRANULE, mmu_idx, ra);
    } else {
        probe_write(env, ptr, TAG_GRANULE, mmu_idx, ra);
        probe_write(env, ptr + TAG_GRANULE, TAG_GRANULE, mmu_idx, ra);
    }
}

uint64_t HELPER(ldgm)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();
    int gm_bs = env_archcpu(env)->gm_blocksize;
    int gm_bs_bytes = 4 << gm_bs;
    void *tag_mem;
    uint64_t ret;
    int shift;

    ptr = QEMU_ALIGN_DOWN(ptr, gm_bs_bytes);

    tag_mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_LOAD,
                                 gm_bs_bytes, MMU_DATA_LOAD, ra);

    if (!tag_mem) {
        return 0;
    }

    switch (gm_bs) {
    case 3:
        ret = *(uint8_t *)tag_mem;
        break;
    case 4:
        ret = cpu_to_le16(*(uint16_t *)tag_mem);
        break;
    case 5:
        ret = cpu_to_le32(*(uint32_t *)tag_mem);
        break;
    case 6:
        return cpu_to_le64(*(uint64_t *)tag_mem);
    default:
        g_assert_not_reached();
    }
    shift = extract64(ptr, LOG2_TAG_GRANULE, 4) * 4;
    return ret << shift;
}

void HELPER(stgm)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();
    int gm_bs = env_archcpu(env)->gm_blocksize;
    int gm_bs_bytes = 4 << gm_bs;
    void *tag_mem;
    int shift;

    ptr = QEMU_ALIGN_DOWN(ptr, gm_bs_bytes);

    tag_mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                 gm_bs_bytes, MMU_DATA_LOAD, ra);

    if (!tag_mem) {
        return;
    }

    shift = extract64(ptr, LOG2_TAG_GRANULE, 4) * 4;
    val >>= shift;
    switch (gm_bs) {
    case 3:
        *(uint8_t *)tag_mem = val;
        break;
    case 4:
        *(uint16_t *)tag_mem = cpu_to_le16(val);
        break;
    case 5:
        *(uint32_t *)tag_mem = cpu_to_le32(val);
        break;
    case 6:
        *(uint64_t *)tag_mem = cpu_to_le64(val);
        break;
    default:
        g_assert_not_reached();
    }
}

void HELPER(stzgm_tags)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = arm_env_mmu_index(env);
    int log2_dcz_bytes, log2_tag_bytes;
    intptr_t dcz_bytes, tag_bytes;
    uint8_t *mem;

    log2_dcz_bytes = env_archcpu(env)->dcz_blocksize + 2;
    log2_tag_bytes = log2_dcz_bytes - (LOG2_TAG_GRANULE + 1);
    dcz_bytes = (intptr_t)1 << log2_dcz_bytes;
    tag_bytes = (intptr_t)1 << log2_tag_bytes;
    ptr &= -dcz_bytes;

    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE, dcz_bytes,
                             MMU_DATA_STORE, ra);
    if (mem) {
        int tag_pair = (val & 0xf) * 0x11;
        memset(mem, tag_pair, tag_bytes);
    }
}

static void mte_sync_check_fail(CPUARMState *env, uint32_t desc,
                                uint64_t dirty_ptr, uintptr_t ra)
{
    int is_write, syn;

    env->exception.vaddress = dirty_ptr;

    is_write = FIELD_EX32(desc, MTEDESC, WRITE);
    syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, is_write,
                                0x11);
    raise_exception_ra(env, EXCP_DATA_ABORT, syn, exception_target_el(env), ra);
    g_assert_not_reached();
}

static void mte_async_check_fail(CPUARMState *env, uint64_t dirty_ptr,
                                 uintptr_t ra, ARMMMUIdx arm_mmu_idx, int el)
{
    int select;

    if (regime_has_2_ranges(arm_mmu_idx)) {
        select = extract64(dirty_ptr, 55, 1);
    } else {
        select = 0;
    }
    env->cp15.tfsr_el[el] |= 1 << select;
#ifdef CONFIG_USER_ONLY
    cpu_exit(env_cpu(env));
#endif
}

void mte_check_fail(CPUARMState *env, uint32_t desc,
                    uint64_t dirty_ptr, uintptr_t ra)
{
    int mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    ARMMMUIdx arm_mmu_idx = core_to_aa64_mmu_idx(mmu_idx);
    int el, reg_el, tcf;
    uint64_t sctlr;

    reg_el = regime_el(arm_mmu_idx);
    sctlr = env->cp15.sctlr_el[reg_el];

    switch (arm_mmu_idx) {
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E20_0:
        el = 0;
        tcf = extract64(sctlr, 38, 2);
        break;
    default:
        el = reg_el;
        tcf = extract64(sctlr, 40, 2);
    }

    switch (tcf) {
    case 1:
        mte_sync_check_fail(env, desc, dirty_ptr, ra);
        break;

    case 0:
        g_assert_not_reached();

    case 2:
        mte_async_check_fail(env, dirty_ptr, ra, arm_mmu_idx, el);
        break;

    case 3:
        if (FIELD_EX32(desc, MTEDESC, WRITE)) {
            mte_async_check_fail(env, dirty_ptr, ra, arm_mmu_idx, el);
        } else {
            mte_sync_check_fail(env, desc, dirty_ptr, ra);
        }
        break;
    }
}

static int checkN(uint8_t *mem, int odd, int cmp, int count)
{
    int n = 0, diff;

    cmp *= 0x11;
    diff = *mem++ ^ cmp;

    if (odd) {
        goto start_odd;
    }

    while (1) {
        if (unlikely((diff) & 0x0f)) {
            break;
        }
        if (++n == count) {
            break;
        }

    start_odd:
        if (unlikely((diff) & 0xf0)) {
            break;
        }
        if (++n == count) {
            break;
        }

        diff = *mem++ ^ cmp;
    }
    return n;
}

static int checkNrev(uint8_t *mem, int odd, int cmp, int count)
{
    int n = 0, diff;

    cmp *= 0x11;
    diff = *mem-- ^ cmp;

    if (!odd) {
        goto start_even;
    }

    while (1) {
        if (unlikely((diff) & 0xf0)) {
            break;
        }
        if (++n == count) {
            break;
        }

    start_even:
        if (unlikely((diff) & 0x0f)) {
            break;
        }
        if (++n == count) {
            break;
        }

        diff = *mem-- ^ cmp;
    }
    return n;
}

static int mte_probe_int(CPUARMState *env, uint32_t desc, uint64_t ptr,
                         uintptr_t ra, uint64_t *fault)
{
    int mmu_idx, ptr_tag, bit55;
    uint64_t ptr_last, prev_page, next_page;
    uint64_t tag_first, tag_last;
    uint32_t sizem1, tag_count, n, c;
    uint8_t *mem1, *mem2;
    MMUAccessType type;

    bit55 = extract64(ptr, 55, 1);
    *fault = ptr;

    if (unlikely(!tbi_check(desc, bit55))) {
        return -1;
    }

    ptr_tag = allocation_tag_from_addr(ptr);

    if (tcma_check(desc, bit55, ptr_tag)) {
        return 1;
    }

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    type = FIELD_EX32(desc, MTEDESC, WRITE) ? MMU_DATA_STORE : MMU_DATA_LOAD;
    sizem1 = FIELD_EX32(desc, MTEDESC, SIZEM1);

    ptr_last = ptr + sizem1;

    tag_first = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_last = QEMU_ALIGN_DOWN(ptr_last, TAG_GRANULE);
    tag_count = ((tag_last - tag_first) / TAG_GRANULE) + 1;

    prev_page = ptr & TARGET_PAGE_MASK;
    next_page = prev_page + TARGET_PAGE_SIZE;

    if (likely(tag_last - prev_page < TARGET_PAGE_SIZE)) {
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, type, sizem1 + 1,
                                  MMU_DATA_LOAD, ra);
        if (!mem1) {
            return 1;
        }
        n = checkN(mem1, ptr & TAG_GRANULE, ptr_tag, tag_count);
    } else {
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, type, next_page - ptr,
                                  MMU_DATA_LOAD, ra);

        mem2 = allocation_tag_mem(env, mmu_idx, next_page, type,
                                  ptr_last - next_page + 1,
                                  MMU_DATA_LOAD, ra);

        n = c = (next_page - tag_first) / TAG_GRANULE;
        if (mem1) {
            n = checkN(mem1, ptr & TAG_GRANULE, ptr_tag, c);
        }
        if (n == c) {
            if (!mem2) {
                return 1;
            }
            n += checkN(mem2, 0, ptr_tag, tag_count - c);
        }
    }

    if (likely(n == tag_count)) {
        return 1;
    }

    if (n > 0) {
        *fault = tag_first + n * TAG_GRANULE;
    }
    return 0;
}

uint64_t mte_check(CPUARMState *env, uint32_t desc, uint64_t ptr, uintptr_t ra)
{
    uint64_t fault;
    int ret = mte_probe_int(env, desc, ptr, ra, &fault);

    if (unlikely(ret == 0)) {
        mte_check_fail(env, desc, fault, ra);
    } else if (ret < 0) {
        return ptr;
    }
    return useronly_clean_ptr(ptr);
}

uint64_t HELPER(mte_check)(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    unsigned align = FIELD_EX32(desc, MTEDESC, ALIGN);
    if (unlikely(align)) {
        align = (1u << align) - 1;
        if (unlikely(ptr & align)) {
            int idx = FIELD_EX32(desc, MTEDESC, MIDX);
            bool w = FIELD_EX32(desc, MTEDESC, WRITE);
            MMUAccessType type = w ? MMU_DATA_STORE : MMU_DATA_LOAD;
            arm_cpu_do_unaligned_access(env_cpu(env), ptr, type, idx, GETPC());
        }
    }

    return mte_check(env, desc, ptr, GETPC());
}

bool mte_probe(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    uint64_t fault;
    int ret = mte_probe_int(env, desc, ptr, 0, &fault);

    return ret != 0;
}

uint64_t HELPER(mte_check_zva)(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    uintptr_t ra = GETPC();
    int log2_dcz_bytes, log2_tag_bytes;
    int mmu_idx, bit55;
    intptr_t dcz_bytes, tag_bytes, i;
    void *mem;
    uint64_t ptr_tag, mem_tag, align_ptr;

    bit55 = extract64(ptr, 55, 1);

    if (unlikely(!tbi_check(desc, bit55))) {
        return ptr;
    }

    ptr_tag = allocation_tag_from_addr(ptr);

    if (tcma_check(desc, bit55, ptr_tag)) {
        goto done;
    }

    log2_dcz_bytes = env_archcpu(env)->dcz_blocksize + 2;
    log2_tag_bytes = log2_dcz_bytes - (LOG2_TAG_GRANULE + 1);
    dcz_bytes = (intptr_t)1 << log2_dcz_bytes;
    tag_bytes = (intptr_t)1 << log2_tag_bytes;
    align_ptr = ptr & -dcz_bytes;

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    (void) probe_write(env, ptr, 1, mmu_idx, ra);
    mem = allocation_tag_mem(env, mmu_idx, align_ptr, MMU_DATA_STORE,
                             dcz_bytes, MMU_DATA_LOAD, ra);
    if (!mem) {
        goto done;
    }

    switch (log2_tag_bytes) {
    case 0:
        mem_tag = *(uint8_t *)mem;
        ptr_tag *= 0x11u;
        break;
    case 1:
        mem_tag = cpu_to_le16(*(uint16_t *)mem);
        ptr_tag *= 0x1111u;
        break;
    case 2:
        mem_tag = cpu_to_le32(*(uint32_t *)mem);
        ptr_tag *= 0x11111111u;
        break;
    case 3:
        mem_tag = cpu_to_le64(*(uint64_t *)mem);
        ptr_tag *= 0x1111111111111111ull;
        break;

    default:
        ptr_tag *= 0x1111111111111111ull;
        i = 0;
        do {
            mem_tag = cpu_to_le64(*(uint64_t *)(mem + i));
            if (unlikely(mem_tag != ptr_tag)) {
                goto fail;
            }
            i += 8;
            align_ptr += 16 * TAG_GRANULE;
        } while (i < tag_bytes);
        goto done;
    }

    if (likely(mem_tag == ptr_tag)) {
        goto done;
    }

 fail:
    i = ctz64(mem_tag ^ ptr_tag) >> 4;
    mte_check_fail(env, desc, align_ptr + i * TAG_GRANULE, ra);

 done:
    return useronly_clean_ptr(ptr);
}

uint64_t mte_mops_probe(CPUARMState *env, uint64_t ptr, uint64_t size,
                        uint32_t desc)
{
    int mmu_idx, tag_count;
    uint64_t ptr_tag, tag_first, tag_last;
    void *mem;
    bool w = FIELD_EX32(desc, MTEDESC, WRITE);
    uint32_t n;

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    mem = allocation_tag_mem_probe(env, mmu_idx, ptr,
                                   w ? MMU_DATA_STORE : MMU_DATA_LOAD,
                                   size, MMU_DATA_LOAD, true, 0);
    if (!mem) {
        return size;
    }

    ptr_tag = allocation_tag_from_addr(ptr);
    tag_first = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_last = QEMU_ALIGN_DOWN(ptr + size - 1, TAG_GRANULE);
    tag_count = ((tag_last - tag_first) / TAG_GRANULE) + 1;
    n = checkN(mem, ptr & TAG_GRANULE, ptr_tag, tag_count);
    if (likely(n == tag_count)) {
        return size;
    }

    if (n == 0) {
        return 0;
    } else {
        return n * TAG_GRANULE - (ptr - tag_first);
    }
}

uint64_t mte_mops_probe_rev(CPUARMState *env, uint64_t ptr, uint64_t size,
                            uint32_t desc)
{
    int mmu_idx, tag_count;
    uint64_t ptr_tag, tag_first, tag_last;
    void *mem;
    bool w = FIELD_EX32(desc, MTEDESC, WRITE);
    uint32_t n;

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    mem = allocation_tag_mem_probe(env, mmu_idx, ptr,
                                   w ? MMU_DATA_STORE : MMU_DATA_LOAD,
                                   1, MMU_DATA_LOAD, true, 0);
    if (!mem) {
        return size;
    }

    ptr_tag = allocation_tag_from_addr(ptr);
    tag_first = QEMU_ALIGN_DOWN(ptr - (size - 1), TAG_GRANULE);
    tag_last = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_count = ((tag_last - tag_first) / TAG_GRANULE) + 1;
    n = checkNrev(mem, ptr & TAG_GRANULE, ptr_tag, tag_count);
    if (likely(n == tag_count)) {
        return size;
    }

    if (n == 0) {
        return 0;
    } else {
        return (n - 1) * TAG_GRANULE + ((ptr + 1) - tag_last);
    }
}

void mte_mops_set_tags(CPUARMState *env, uint64_t ptr, uint64_t size,
                       uint32_t desc)
{
    int mmu_idx, tag_count;
    uint64_t ptr_tag;
    void *mem;

    if (!desc) {
        return;
    }

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    mem = allocation_tag_mem_probe(env, mmu_idx, ptr, MMU_DATA_STORE, size,
                                   MMU_DATA_STORE, true, 0);
    if (!mem) {
        return;
    }

    ptr_tag = allocation_tag_from_addr(ptr);
    tag_count = size / TAG_GRANULE;
    if (ptr & TAG_GRANULE) {
        store_tag1_parallel(TAG_GRANULE, mem, ptr_tag);
        mem++;
        tag_count--;
    }
    memset(mem, ptr_tag | (ptr_tag << 4), tag_count / 2);
    if (tag_count & 1) {
        mem += tag_count / 2;
        store_tag1_parallel(0, mem, ptr_tag);
    }
}
