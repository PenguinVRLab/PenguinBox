/*
 * PenguinBox — the "sim" title registry and the pre-flight boot gate
 *
 * Design of record: docs/features/sbc-zero-setup-plan.md §4 W1b (the decision
 * record — hook point, the two measured title IDs, the four invariants) and
 * docs/features/sbc-analog-input.md §10 (the checklist flow, the verdict
 * levels, the Skip rule). This header is the contract between the two halves:
 *
 *   REGISTRY  — "is this title a sim, and what hardware does it want?"  Pure
 *               data plus a lookup. No QEMU, no UI, no I/O.
 *   GATE      — "the guest just mapped an XBE; hold the boot if it is a sim."
 *               Owns the probe timer and the vm_stop()/vm_start() pair.
 *
 * The checklist itself is NOT here: it is the existing diagnostics panel
 * (ui/xui/sbc-diagnostics.*) in a gating role — the same window in a different
 * mode, never a second one (W1b invariant 3). The panel pulls
 * sim_gate_active_profile() every UI frame and releases the VM through
 * sim_gate_release().
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

#ifndef XEMU_SIM_PROFILES_H
#define XEMU_SIM_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * REGISTRY
 * ---------------------------------------------------------------------------
 * Keyed on XBE title ID — NOT name, not hash — because that is the same key
 * the later per-game profile system (sbc-analog-input.md §8) uses, so a full
 * profile can be hung off an entry without re-keying anything.
 *
 * This is deliberately the MINIMUM schema W1b asks for: enough to answer "is
 * anything blocking?" and nothing more. Full per-game control profiles (§8)
 * and calibration persistence (§9) are later workstreams and must not be
 * back-fitted here.
 */

/* Device classes a sim can ask for. One today; the enum exists so a second
 * (wheel, flight stick) is an added enumerator plus a case in the checklist,
 * not a rewrite. */
typedef enum SimDeviceKind {
    /* Real Steel Battalion controller, via the host reader contract in
     * ui/xemu-sbc-device.h. */
    SIM_DEV_SBC = 0,
} SimDeviceKind;

typedef struct SimRequirement {
    SimDeviceKind device;
    /* §10 step 3: `required` absent => blocking; `optional` absent => warning
     * (the overlay or a fallback binding covers it) and the boot proceeds. */
    bool required;
    /* Shown verbatim in the checklist row. */
    const char *label;
} SimRequirement;

typedef struct SimProfile {
    uint32_t title_id;
    const char *display_name;
    const SimRequirement *requirements;
    unsigned num_requirements;
    /* One sentence of context for the checklist header; "" is fine. */
    const char *notes;
} SimProfile;

/* Returns the profile for `title_id`, or NULL when the title is not a sim.
 * Cheap linear scan over a handful of entries; called exactly once per boot. */
const SimProfile *sim_profile_lookup(uint32_t title_id);

/*
 * ---------------------------------------------------------------------------
 * GATE
 * ---------------------------------------------------------------------------
 */

/*
 * Arm the title probe. Call once, at machine start, with the BQL held.
 *
 * `device_layer_up` is the host device layer's OWN answer — the return value
 * of sbc_device_init(). It is false when the host reader is disabled or the
 * build has no libusb, and that is exactly W1b invariant 4: a recognised sim
 * must then degrade to "no device layer - nothing to check" and let the user
 * straight through. The gate never asks a user to satisfy a feature they did
 * not turn on.
 *
 * No-op (and never probes) when the gate is disabled in config.
 */
void sim_gate_init(bool device_layer_up);

/* Non-NULL only while the gate is holding the VM for a recognised sim. The
 * diagnostics panel polls this every UI frame; NULL means "draw the ordinary
 * Machine-menu panel". */
const SimProfile *sim_gate_active_profile(void);

/* Whether there is a host device layer to check at all (see sim_gate_init). */
bool sim_gate_device_layer_up(void);

/*
 * Release the boot. `skipped` records which button the user pressed — Continue
 * (false) or the deliberate Skip (true) — for the log line only; both resume
 * the guest. Idempotent, and a no-op when the gate is not up. Must be called
 * with the BQL held (the UI frame holds it).
 */
void sim_gate_release(bool skipped);

#ifdef __cplusplus
}
#endif

#endif /* XEMU_SIM_PROFILES_H */
