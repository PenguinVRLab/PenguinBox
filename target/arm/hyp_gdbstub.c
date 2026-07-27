/*
 * ARM implementation of KVM and HVF hooks, 64 bit specific code
 *
 * Copyright Mian-M. Hamayun 2013, Virtual Open Systems
 * Copyright Alex Bennée 2014, Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "gdbstub/enums.h"

int max_hw_bps, max_hw_wps;
GArray *hw_breakpoints, *hw_watchpoints;

int insert_hw_breakpoint(vaddr addr)
{
    HWBreakpoint brk = {
        .bcr = 0x1,
        .bvr = sextract64(addr, 0, 53)
    };

    if (cur_hw_bps >= max_hw_bps) {
        return -ENOBUFS;
    }

    brk.bcr = deposit32(brk.bcr, 1, 2, 0x3);
    brk.bcr = deposit32(brk.bcr, 5, 4, 0xf);

    g_array_append_val(hw_breakpoints, brk);

    return 0;
}

int delete_hw_breakpoint(vaddr pc)
{
    int i;
    for (i = 0; i < hw_breakpoints->len; i++) {
        HWBreakpoint *brk = get_hw_bp(i);
        if (brk->bvr == pc) {
            g_array_remove_index(hw_breakpoints, i);
            return 0;
        }
    }
    return -ENOENT;
}

int insert_hw_watchpoint(vaddr addr, vaddr len, int type)
{
    HWWatchpoint wp = {
        .wcr = R_DBGWCR_E_MASK,
        .wvr = addr & (~0x7ULL),
        .details = { .vaddr = addr, .len = len }
    };

    if (cur_hw_wps >= max_hw_wps) {
        return -ENOBUFS;
    }

    wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, PAC, 3);

    switch (type) {
    case GDB_WATCHPOINT_READ:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 1);
        wp.details.flags = BP_MEM_READ;
        break;
    case GDB_WATCHPOINT_WRITE:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 2);
        wp.details.flags = BP_MEM_WRITE;
        break;
    case GDB_WATCHPOINT_ACCESS:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 3);
        wp.details.flags = BP_MEM_ACCESS;
        break;
    default:
        g_assert_not_reached();
    }
    if (len <= 8) {
        int off = addr & 0x7;
        int bas = (1 << len) - 1;

        wp.wcr = deposit32(wp.wcr, 5 + off, 8 - off, bas);
    } else {
        if (is_power_of_2(len)) {
            int bits = ctz64(len);

            wp.wvr &= ~((1 << bits) - 1);
            wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, MASK, bits);
            wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, BAS, 0xff);
        } else {
            return -ENOBUFS;
        }
    }

    g_array_append_val(hw_watchpoints, wp);
    return 0;
}

bool check_watchpoint_in_range(int i, vaddr addr)
{
    HWWatchpoint *wp = get_hw_wp(i);
    uint64_t addr_top, addr_bottom = wp->wvr;
    int bas = extract32(wp->wcr, 5, 8);
    int mask = extract32(wp->wcr, 24, 4);

    if (mask) {
        addr_top = addr_bottom + (1 << mask);
    } else {
        addr_bottom = addr_bottom + ctz32(bas);
        addr_top = addr_bottom + clo32(bas);
    }

    if (addr >= addr_bottom && addr <= addr_top) {
        return true;
    }

    return false;
}

int delete_hw_watchpoint(vaddr addr, vaddr len, int type)
{
    int i;
    for (i = 0; i < cur_hw_wps; i++) {
        if (check_watchpoint_in_range(i, addr)) {
            g_array_remove_index(hw_watchpoints, i);
            return 0;
        }
    }
    return -ENOENT;
}

bool find_hw_breakpoint(CPUState *cpu, vaddr pc)
{
    int i;

    for (i = 0; i < cur_hw_bps; i++) {
        HWBreakpoint *bp = get_hw_bp(i);
        if (bp->bvr == pc) {
            return true;
        }
    }
    return false;
}

CPUWatchpoint *find_hw_watchpoint(CPUState *cpu, vaddr addr)
{
    int i;

    for (i = 0; i < cur_hw_wps; i++) {
        if (check_watchpoint_in_range(i, addr)) {
            return &get_hw_wp(i)->details;
        }
    }
    return NULL;
}
