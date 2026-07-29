/*
 * PenguinBox — the "sim" title registry and the pre-flight boot gate
 *
 * Implements ui/xemu-sim-profiles.h. See that header for the split between the
 * registry (pure data) and the gate (probe timer + vm_stop/vm_start), and
 * docs/features/sbc-zero-setup-plan.md §4 W1b for the decision record.
 *
 * ---------------------------------------------------------------------------
 * WHY THE HOOK POINT IS "PROBE GUEST MEMORY, THEN PAUSE" (W1b, settled)
 * ---------------------------------------------------------------------------
 * Reading default.xbe off the disc before boot is not available: there is no
 * XDVDFS/XISO parser in the tree, and xemu_get_xbe_info() reads the header out
 * of GUEST memory at 0x10000 (xemu-xbe.c:82), so nothing can answer "which
 * title is this?" until the guest loader has mapped it. That is early enough:
 * the XBE header is mapped before the title's entry point runs, and therefore
 * before the game enumerates controllers — which is the property that actually
 * matters, because a controller fixed at the checklist is still seen by the
 * game.
 *
 * The probe itself is the one already proven in hw/xbox/xbox-game-hacks.c:
 * 'XBEH' magic at 0x10000, certificate VA at +0x118, title ID at +0x8. A
 * failed read is NORMAL and means "not resident yet, try again", never an
 * error — the address is simply not mapped during BIOS/boot.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR THINGS THIS FILE IS CAREFUL ABOUT
 * ---------------------------------------------------------------------------
 * 1. LATCH ONCE, THEN STOP FOREVER (invariant 1). The first SUCCESSFUL title
 *    read is the last one, match or no match: the timer is not re-armed. A
 *    non-sim title pays one boolean read of a latched flag and nothing else —
 *    no modal, no polling, no visible change.
 *
 *    Consequence, accepted deliberately: if the guest boots the dashboard
 *    first and the user then launches a sim from it, the latch has already
 *    fired on the dashboard's title and there is no gate. Booting with the
 *    disc inserted (the normal path, and what the launchers do) loads the
 *    title's XBE directly, which is the case the gate is for.
 *
 * 2. QEMU_CLOCK_REALTIME, never the virtual clock. The virtual clock stalls
 *    while the VM is paused, so a virtual-clock timer would wedge itself the
 *    instant the gate did its job.
 *
 * 3. THE DEVICE LAYER IS THE AUTHORITY ON WHETHER THERE IS ANYTHING TO CHECK
 *    (invariant 4). sim_gate_init() takes sbc_device_init()'s own return value
 *    rather than re-deriving "is the reader armed?" from environment variables,
 *    which would be a second copy of that rule waiting to drift. The flag
 *    defaults to false, so if anything ever races, it races towards "let the
 *    user through".
 *
 * 4. NOTHING HERE DRAWS. The gate publishes a profile pointer; the existing
 *    diagnostics panel picks it up and becomes the checklist (invariant 3).
 *
 * Copyright (C) 2026 the PenguinBox authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h" /* ldl_le_p */
#include "qemu/timer.h"
#include "hw/core/cpu.h" /* qemu_get_cpu, cpu_memory_rw_debug */
#include "system/runstate.h"

#include "xemu-settings.h"
#include "xemu-sim-profiles.h"

#define SIM_LOG_PREFIX "sim-gate: "

/* ------------------------------------------------------------------------ */
/* Registry                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Every sim in the table today wants the same thing: a real Steel Battalion
 * controller. Sharing one requirement array is what makes an entry a single
 * line — the point of SIM_ENTRY below.
 */
static const SimRequirement sim_req_sbc[] = {
    { SIM_DEV_SBC, true, "Steel Battalion controller" },
};

#define SIM_ENTRY(id, name, reqs, note) \
    { (id), (name), (reqs), ARRAY_SIZE(reqs), (note) }

/*
 * ADDING A SIM IS ONE LINE. Title IDs are the XBE certificate title ID, read
 * off the retail discs (W1b, measured 2026-07-27); both of these are Capcom
 * (0x4343). 0x43430002 also confirms the constant hard-coded in
 * hw/xbox/xbox-game-hacks.c.
 */
static const SimProfile sim_profiles[] = {
    SIM_ENTRY(0x43430002u, "Steel Battalion", sim_req_sbc,
              "40 buttons, three pedals and two levers - the title with the "
              "most hardware to lose."),
    SIM_ENTRY(0x43430009u, "Steel Battalion: Line of Contact", sim_req_sbc,
              "Same controller as Steel Battalion; the online sequel."),
};

const SimProfile *sim_profile_lookup(uint32_t title_id)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(sim_profiles); i++) {
        if (sim_profiles[i].title_id == title_id) {
            return &sim_profiles[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Gate                                                                      */
/* ------------------------------------------------------------------------ */

/* XBE header constants — identical to hw/xbox/xbox-game-hacks.c, which is the
 * proven copy. Kept local rather than exported from there: that file is a
 * target-specific perf hack and this is common UI code. */
#define XBE_MAGIC            0x48454258u /* 'XBEH' */
#define XBE_HDR_VADDR        0x00010000u
#define XBE_HDR_CERT_OFF     0x118u /* offsetof(xbe_header, m_certificate_addr) */
#define XBE_CERT_TITLEID_OFF 0x8u   /* offsetof(xbe_certificate, m_titleid) */

/* The certificate always lives inside the XBE header block, which
 * xemu-xbe.c:108 caps at 8 pages. Bounding the VA before dereferencing it
 * means a torn read during a page-table change cannot manufacture a plausible
 * title ID out of noise and open a gate on the wrong game. */
#define XBE_HDR_MAX_LEN 0x8000u

/* The boot window is seconds long, so a few hundred ms is plenty and costs
 * nothing measurable. */
/* Fast while we have never identified a title (the boot window is seconds and
 * we want to catch the XBE the moment the loader maps it, before the game
 * enumerates controllers). Slow once a title is known: from then on we are
 * only watching for a title CHANGE, e.g. the dashboard launching a disc. */
#define SIM_PROBE_INTERVAL_MS 250
#define SIM_PROBE_IDLE_MS 1000

/*
 * THREADING. `gating` is written by the probe timer (QEMU main-loop thread)
 * and read by the diagnostics panel (the SDL display thread) — but the panel
 * only ever draws from inside xemu_hud_update(), which xemu.c calls under
 * xemu_main_loop_lock(), i.e. holding the BQL, and main-loop timers run under
 * the BQL too. Both sides are therefore serialised by the BQL and no extra
 * lock or atomic is needed here.
 */
static struct {
    QEMUTimer *timer;
    bool armed;            /* sim_gate_init() has run */
    bool disabled;         /* config says off: never probe at all */
    bool device_layer_up;  /* sbc_device_init() succeeded */
    bool stopped_by_us;    /* we called vm_stop() and owe a vm_start() */
    const SimProfile *gating; /* non-NULL while the VM is held */
    /* Last title the probe saw. We gate on the TRANSITION into a registered
     * sim, not on the first read ever: booting to the dashboard and launching
     * the disc from there is a normal way to start a game, and a
     * first-read-wins latch would burn itself on the dashboard's own title and
     * silently never gate. Tracking the transition costs one u32 compare. */
    uint32_t last_title;
} sim_gate;

/* Read a little-endian guest u32 through the current page tables. Returns
 * false when the address is not mapped — the expected answer for most of the
 * boot, not an error. */
static bool sim_read_guest_u32(CPUState *cs, uint32_t vaddr, uint32_t *out)
{
    uint8_t b[4];

    if (cpu_memory_rw_debug(cs, vaddr, b, sizeof(b), false) != 0) {
        return false;
    }
    *out = (uint32_t)ldl_le_p(b);
    return true;
}

/* Probe the resident XBE's certificate title ID. Returns false for "no XBE
 * resident yet"; the caller retries. Deliberately does not use
 * xemu_get_xbe_info(), which allocates and returns a shared static. */
static bool sim_probe_title_id(uint32_t *out)
{
    CPUState *cs = qemu_get_cpu(0); /* the Xbox is single-CPU */
    uint32_t magic, cert_va, title;

    if (cs == NULL) {
        return false;
    }
    if (!sim_read_guest_u32(cs, XBE_HDR_VADDR, &magic) || magic != XBE_MAGIC) {
        return false;
    }
    if (!sim_read_guest_u32(cs, XBE_HDR_VADDR + XBE_HDR_CERT_OFF, &cert_va)) {
        return false;
    }
    if (cert_va < XBE_HDR_VADDR ||
        cert_va >= XBE_HDR_VADDR + XBE_HDR_MAX_LEN) {
        return false;
    }
    if (!sim_read_guest_u32(cs, cert_va + XBE_CERT_TITLEID_OFF, &title)) {
        return false;
    }

    *out = title;
    return true;
}

static void sim_gate_tick(void *opaque)
{
    const SimProfile *profile;
    uint32_t title_id = 0;

    (void)opaque;

    if (sim_gate.disabled || sim_gate.gating != NULL) {
        /* Disabled, or already holding the boot: nothing to do. Re-arm only
         * in the latter case, so the probe resumes after the user continues. */
        if (sim_gate.gating != NULL) {
            timer_mod(sim_gate.timer,
                      qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                          SIM_PROBE_IDLE_MS);
        }
        return;
    }

    /* Always re-arm. INVARIANT 1 is about cost, not about stopping: a title
     * that is not a registered sim pays one 4-byte guest read per idle tick
     * and nothing else — no modal, no allocation, no visible change. The
     * probe has to keep running because the title can CHANGE (dashboard ->
     * disc), and that transition is exactly what we must not miss. */
    timer_mod(sim_gate.timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                  (sim_gate.last_title ? SIM_PROBE_IDLE_MS
                                       : SIM_PROBE_INTERVAL_MS));

    if (!sim_probe_title_id(&title_id)) {
        return; /* not mapped yet — the normal answer for most of the boot */
    }

    if (title_id == sim_gate.last_title) {
        return; /* same title still running: not a boot, nothing to gate */
    }
    sim_gate.last_title = title_id;

    profile = sim_profile_lookup(title_id);
    if (profile == NULL) {
        fprintf(stderr, SIM_LOG_PREFIX
                "title 0x%08X is not a registered sim; no pre-flight check\n",
                (unsigned)title_id);
        return;
    }

    fprintf(stderr, SIM_LOG_PREFIX "title 0x%08X = %s; holding the boot for "
            "the pre-flight check\n", (unsigned)title_id,
            profile->display_name);

    sim_gate.gating = profile;
    if (runstate_is_running()) {
        /* Safe from here: this callback runs on the main-loop thread (never a
         * vCPU thread — vm_stop() has its own path for that), which is the
         * same context the monitor's `stop` command uses, and our timer has
         * already been popped off the active list before the callback ran, so
         * the aio polling inside do_vm_stop() cannot re-enter it. */
        sim_gate.stopped_by_us = true;
        vm_stop(RUN_STATE_PAUSED);
    } else {
        /* Already paused by the user. Show the checklist anyway, but do not
         * hand back a running VM they did not ask for. */
        sim_gate.stopped_by_us = false;
    }
}

void sim_gate_init(bool device_layer_up)
{
    if (sim_gate.armed) {
        return;
    }
    sim_gate.armed = true;
    sim_gate.device_layer_up = device_layer_up;

    if (!g_config.input.sim_preflight_gate) {
        /* Disabled in config: no timer is ever created, so nothing is ever
         * probed. Invariant 1, taken to its limit. */
        sim_gate.disabled = true;
        return;
    }

    sim_gate.timer = timer_new_ms(QEMU_CLOCK_REALTIME, sim_gate_tick, NULL);
    timer_mod(sim_gate.timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + SIM_PROBE_INTERVAL_MS);
}

const SimProfile *sim_gate_active_profile(void)
{
    return sim_gate.gating;
}

bool sim_gate_device_layer_up(void)
{
    return sim_gate.device_layer_up;
}

void sim_gate_release(bool skipped)
{
    if (sim_gate.gating == NULL) {
        return;
    }

    fprintf(stderr, SIM_LOG_PREFIX "%s for %s; resuming\n",
            skipped ? "SKIPPED by the user" : "checklist cleared",
            sim_gate.gating->display_name);

    sim_gate.gating = NULL;
    if (sim_gate.stopped_by_us) {
        sim_gate.stopped_by_us = false;
        vm_start();
    }
}
