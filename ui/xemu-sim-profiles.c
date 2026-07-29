/*
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
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "system/runstate.h"

#include "xemu-settings.h"
#include "xemu-sim-profiles.h"

#define SIM_LOG_PREFIX "sim-gate: "

static const SimRequirement sim_req_sbc[] = {
    { SIM_DEV_SBC, true, "Steel Battalion controller" },
};

#define SIM_ENTRY(id, name, reqs, note) \
    { (id), (name), (reqs), ARRAY_SIZE(reqs), (note) }

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

#define XBE_MAGIC            0x48454258u
#define XBE_HDR_VADDR        0x00010000u
#define XBE_HDR_CERT_OFF     0x118u
#define XBE_CERT_TITLEID_OFF 0x8u

#define XBE_HDR_MAX_LEN 0x8000u

#define SIM_PROBE_INTERVAL_MS 250
#define SIM_PROBE_IDLE_MS 1000

static struct {
    QEMUTimer *timer;
    bool armed;
    bool disabled;
    bool device_layer_up;
    bool stopped_by_us;
    const SimProfile *gating;
    uint32_t last_title;
} sim_gate;

static bool sim_read_guest_u32(CPUState *cs, uint32_t vaddr, uint32_t *out)
{
    uint8_t b[4];

    if (cpu_memory_rw_debug(cs, vaddr, b, sizeof(b), false) != 0) {
        return false;
    }
    *out = (uint32_t)ldl_le_p(b);
    return true;
}

static bool sim_probe_title_id(uint32_t *out)
{
    CPUState *cs = qemu_get_cpu(0);
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
        if (sim_gate.gating != NULL) {
            timer_mod(sim_gate.timer,
                      qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                          SIM_PROBE_IDLE_MS);
        }
        return;
    }

    timer_mod(sim_gate.timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                  (sim_gate.last_title ? SIM_PROBE_IDLE_MS
                                       : SIM_PROBE_INTERVAL_MS));

    if (!sim_probe_title_id(&title_id)) {
        return;
    }

    if (title_id == sim_gate.last_title) {
        return;
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
        sim_gate.stopped_by_us = true;
        vm_stop(RUN_STATE_PAUSED);
    } else {
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
