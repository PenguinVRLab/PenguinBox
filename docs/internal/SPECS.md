# SPECS.md — the specification index

> PRIVATE — never merged to the public repo. See [README.md](README.md).

One row per spec. **Status**: `missing` → `v1 draft` → `verified` (all
[ASSUMED]s resolved or deliberately accepted) → `stale` (code moved, spec
didn't — mark on sight). The spec-with-change rule
([00-specification-plan.md](00-specification-plan.md) §5): a PR touching a
specced area updates the spec in the same PR.

| Spec | Tier | Status | Last verified | Notes |
|---|---|---|---|---|
| [00-specification-plan.md](00-specification-plan.md) | — | living | 2026-07-27 | The plan itself |
| [A1-vr-subsystem.md](A1-vr-subsystem.md) | A | v1 draft | 2026-07-27 | Consolidates docs/vr/ into contract form |
| [A2-perf-levers.md](A2-perf-levers.md) | A | v1 draft | 2026-07-27 | Master flag table; defaults re-read from code — note predl/tex-dirty-clear default ON |
| [A3-instrumentation.md](A3-instrumentation.md) | A | v1 draft | 2026-07-27 | Every instrument + arming semantics + traps |
| [A4-sbc-overlay.md](A4-sbc-overlay.md) | A | v1 draft | 2026-07-27 | F3 cockpit overlay |
| [A5-game-hacks.md](A5-game-hacks.md) | A | v1 draft | 2026-07-27 | Hack inventory: exactly 1 (SB radar downsample) |
| [A6-build-harness.md](A6-build-harness.md) | A | v1 draft | 2026-07-27 | libusb enforcement gap documented — nothing forces it in-tree yet |
| A7 — perf tooling | A | spec'd elsewhere | 2026-07-27 | Lives at [../perf/tool-specs.md](../perf/tool-specs.md) (T1–T12); migrate here when tools land |
| [B1-surface-cache.md](B1-surface-cache.md) | B | v1 draft | 2026-07-27 | The exemplar; campaign-verified invariants |
| [B2-texture-cache.md](B2-texture-cache.md) | B | v1 draft | 2026-07-27 | TextureKey/borrows + all five S2T paths |
| [B3-format-model.md](B3-format-model.md) | B | v1 draft | 2026-07-27 | Decode-chain doctrine; Morton math |
| [B4-pfifo-present.md](B4-pfifo-present.md) | B | v1 draft | 2026-07-27 | Finish-reason taxonomy; vblank ladder mechanics |
| [B5-layout-model.md](B5-layout-model.md) | B | v1 draft | 2026-07-27 | Pair table, rest states, feedback GENERAL |
| [B6-input-stack.md](B6-input-stack.md) | B | v1 draft | 2026-07-27 | XID devices; maps are driver-selected (scancode-18 was a mode flip, not a collision); found pedal-scancode OOB |
| [B7-config-system.md](B7-config-system.md) | B | v1 draft | 2026-07-27 | yml→genconfig pipeline; validation-arming chain traced (G1 leads); fallback persists to disk |
| [B8-snapshots-loadvm.md](B8-snapshots-loadvm.md) | B | v1 draft | 2026-07-27 | vmstate inventory (NV2A regs in, renderer out); surfaces rebuilt / textures rehash post-load; hotplug -loadvm mechanics |
| [B9-gl-renderer.md](B9-gl-renderer.md) | B | v1 draft | 2026-07-27 | Parity notes; found VK/GL flush asymmetry (triage) |
| [C1-hw-component-cards.md](C1-hw-component-cards.md) | C | v1 draft | 2026-07-27 | ~9 cards, hw/xbox devices |
| [C2-ui-component-cards.md](C2-ui-component-cards.md) | C | v1 draft | 2026-07-27 | ~9 cards, xui + display path |

## Escalation rule

Modifying a Tier-C component upgrades its card to a Tier-B spec **in the same
change**. Card → spec promotions get their own file (`B10+…`) and an index
row here.

## Standing review queue (findings the v1 pass itself surfaced)

Logged here so they don't evaporate; none blocks a spec's draft status:

1. **G1 lead (B7):** the validation-layer arming chain has two unaudited
   seams — the XR-created-instance path (`vk/instance.c:233`) and the
   debug-utils-extension gate (`instance.c:142`). First stop for the T1 fix.
2. **VK/GL flush asymmetry (B9):** VK's flush marks all texture bindings
   dirty; GL's does not — possible stale-texture window after loadvm on GL.
   Assess before any GL-path claim.
3. **libusb enforcement gap (A6):** neither build.sh default, the flatpak
   manifest, nor CI forces libusb — the HANDOFF-packaging change is still
   unimplemented in-tree.
4. **Vestigial finish reason (B4):** `VK_FINISH_REASON_FRAMEBUFFER_DIRTY`
   has no live raise site — candidate for removal or a comment.
   Same class (A3): `NV2A_PROF_ZC_REJ_ZETA_BOUND` is declared but has no
   increment site since the v9.4 revert — permanently reads 0; remove or
   comment.
5. **Defaults false-memory hazard (A2):** predictive readback and
   tex-dirty-clear default ON; scripts and docs written from "everything
   defaults off" are wrong about these two.
6. **Pedal scancodes escape sanitization (B6):** the SBC map's range check
   covers indices 0–48 only; the three pedal keys live at 49–51 — an
   out-of-range `*_pedal` toml value indexes the 512-entry SDL key array out
   of bounds **every poll** (`ui/xemu-input.c:394-410`). Small real bug; fix
   the loop bound.
7. **Guest-reachable asserts on the Duke path (B6):** malformed SET_REPORT
   length / unknown control request / unbound-port poll abort the emulator
   (`xid.c`, `xid-gamepad.c`); the SBC path was hardened (D2/D4) — port the
   same clamp+STALL treatment to the gamepad device.
8. **`config_spec.yml:139` comment error (B6):** scancode 18 is `o`, not
   `w` — the wrong comment seeded the "collision" mischaracterization that
   reached two perf docs (now corrected in both).
9. **FIXED 2026-07-28 — H-3 now has an in-code detector, and the fallback no
   longer persists (B9+B7+A1):** `init_renderer` (`pgraph.c`) now prints a
   framed stderr banner and an explicit VR-inactive line whenever
   `vr.enable` was set and Vulkan failed, drives a persistent UI warning in
   the Display tab and Video Debug overlay
   (`nv2a-renderer-status.h`/`nv2a_get_renderer_fallback_active/reason()`),
   and `xemu_settings_note_requested_renderer()` +
   `xemu_settings_save()` (`ui/xemu-settings.{h,cc}`) ensure a
   fallback-forced renderer is never what gets written to disk — only what
   was actually requested. Chose **loud, not fatal** (no refuse-to-continue)
   so a user who just wants to play flat after a driver hiccup still can,
   informed rather than crashed. Verified by a clean win64-cross build
   (GCC 16); **not yet exercised live** — nobody has deliberately broken VK
   init and watched the banner fire, or confirmed the on-disk config
   survives a restart. That is now the concrete remaining gap.
