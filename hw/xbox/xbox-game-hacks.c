/*
 * Copyright (c) 2026 Patrick Carey
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
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/bswap.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/xbox-game-hacks.h"

#define SB_TITLE_ID          0x43430002u
#define XBE_MAGIC            0x48454258u
#define XBE_HDR_VADDR        0x00010000u
#define XBE_HDR_CERT_OFF     0x118u
#define XBE_CERT_TITLEID_OFF 0x8u

static const uint32_t g_scanner_eip[4] = {
    0x25d40, 0x26460, 0x26b80, 0x27250,
};

static int g_skip_this_frame;
static int g_disabled;

static bool g_want_installed;

static int g_title_state = -1;

static int radar_skip_read_n(void)
{
    static int env_n = -1;
    static const char *watch;

    if (env_n < 0) {
        const char *e = getenv("XEMU_RADAR_SKIP_N");
        env_n = (e && atoi(e) > 0) ? atoi(e) : 0;
        watch = getenv("XEMU_RADAR_SKIP_FILE");
        if (!watch) {
            watch = "/tmp/radar-skip-n";
        }
    }

    int n = env_n;
    FILE *f = fopen(watch, "r");
    if (f) {
        int v;
        if (fscanf(f, "%d", &v) == 1) {
            n = v;
        }
        fclose(f);
    }
    return n;
}

static bool read_guest_u32(CPUState *cs, uint32_t vaddr, uint32_t *out)
{
    uint8_t b[4];
    if (cpu_memory_rw_debug(cs, vaddr, b, sizeof(b), false) != 0) {
        return false;
    }
    *out = (uint32_t)ldl_le_p(b);
    return true;
}

static int probe_title_is_sb(CPUState *cs)
{
    uint32_t magic, cert_va, title;

    if (!read_guest_u32(cs, XBE_HDR_VADDR, &magic) || magic != XBE_MAGIC) {
        return -1;
    }
    if (!read_guest_u32(cs, XBE_HDR_VADDR + XBE_HDR_CERT_OFF, &cert_va) ||
        cert_va == 0) {
        return -1;
    }
    if (!read_guest_u32(cs, cert_va + XBE_CERT_TITLEID_OFF, &title)) {
        return -1;
    }
    return (title == SB_TITLE_ID) ? 1 : 0;
}

static void radar_install_cb(CPUState *cs, run_on_cpu_data data)
{
    if (g_title_state < 0) {
        g_title_state = probe_title_is_sb(cs);
        if (g_title_state == 0) {
            qatomic_set(&g_disabled, 1);
            fprintf(stderr, "radar-skip: loaded title is not Steel Battalion "
                            "(0x43430002); disabled\n");
            return;
        }
        if (g_title_state < 0) {
            return;
        }
        fprintf(stderr, "radar-skip: Steel Battalion detected; "
                        "radar-scan breakpoints armed\n");
    }
    if (g_title_state != 1) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        cpu_breakpoint_remove(cs, g_scanner_eip[i], BP_CPU);
        cpu_breakpoint_insert(cs, g_scanner_eip[i], BP_CPU, NULL);
    }
}

static void radar_remove_cb(CPUState *cs, run_on_cpu_data data)
{
    for (int i = 0; i < 4; i++) {
        cpu_breakpoint_remove(cs, g_scanner_eip[i], BP_CPU);
    }
}

void xbox_game_hacks_frame_tick(void)
{
    int n = radar_skip_read_n();

    static int last_n = INT_MIN;
    if (n != last_n) {
        fprintf(stderr, "radar-skip: N=%d -> %s\n",
                n, (n >= 2) ? "ON (scan 1 frame in N)" : "OFF");
        last_n = n;
    }

    if (qatomic_read(&g_disabled)) {
        n = 0;
    }

    bool active = (n >= 2);
    unsigned fc = g_nv2a_stats.frame_count;
    bool skip = active && ((fc % (unsigned)n) != 0);
    qatomic_set(&g_skip_this_frame, skip ? 1 : 0);

    bool want = skip;
    if (want != g_want_installed) {
        CPUState *cs = qemu_get_cpu(0);
        if (cs) {
            async_safe_run_on_cpu(cs,
                                  want ? radar_install_cb : radar_remove_cb,
                                  RUN_ON_CPU_NULL);
            g_want_installed = want;
        }
    }
}

void xbox_game_hacks_maybe_skip_scanner(CPUState *cs)
{
    if (cs->watchpoint_hit) {
        return;
    }

    CPUX86State *env = &X86_CPU(cs)->env;
    uint32_t eip = env->eip;

    bool ours = false;
    for (int i = 0; i < 4; i++) {
        if (eip == g_scanner_eip[i]) {
            ours = true;
            break;
        }
    }
    if (!ours) {
        return;
    }

    if (qatomic_read(&g_skip_this_frame)) {
        uint32_t esp = (uint32_t)env->regs[R_ESP];
        vaddr sp_lin = env->segs[R_SS].base + esp;
        uint8_t b[4];

        if (cpu_memory_rw_debug(cs, sp_lin, b, sizeof(b), false) == 0) {
            env->eip = (uint32_t)ldl_le_p(b);
            env->regs[R_ESP] = esp + 4;
            nv2a_profile_inc_counter(NV2A_PROF_RADAR_SCAN_SKIP);
        } else {
            env->eflags |= RF_MASK;
        }
    } else {
        env->eflags |= RF_MASK;
    }

    cpu_loop_exit_noexc(cs);
}
