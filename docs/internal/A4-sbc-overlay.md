# Spec — SBC cockpit overlay (desktop mirror)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| A | `ui/xui/sbc-overlay.cc` / `.hh` (hooks: `ui/xui/main.cc:302,329`, `ui/xui/menubar.cc:240-245`, `ui/xui/meson.build:17`; state source: `ui/xemu-input.c:1027-1042`) | v1 draft | 2026-07-27 |

## Purpose & scope

A compact, semi-transparent, **click-through** ImGui window on the desktop HUD
that shows LIVE emulated Steel Battalion Controller state plus the keyboard
binding for each control. It exists because the keyboard/mouse SBC placeholder
has ~49 bindings with zero in-game visibility (the Input-menu rebind table
covers only the 25-entry gamepad map), and because of the ISS-B11 "can't move"
incident — the mech idled in REVERSE and a gear readout would have made it a
non-event.

In scope: display + hotkey + View-menu toggle. Out of scope (deliberate cuts,
`docs/features/sbc-cockpit-overlay.md`): rebinding UI, dragging, gamepad-fed
SBC labels, and **any in-headset rendering** — the ImGui HUD is desktop-only.
The in-headset equivalent is the VR SBC world panel (see the cross-reference
section below and spec `A1-vr-subsystem.md`).

## External interface

**Config keys** — `display.ui.sbc_overlay.*` (`config_spec.yml:528-549`):

| key | type | default |
|---|---|---|
| `enable` | bool | false (persisted; menu/hotkey flip it) |
| `corner` | enum top_left/top_right/bottom_left/bottom_right | bottom_right |
| `opacity` | float | 0.85 (clamped to 0.05..1.0 at draw) |
| `show_key_labels` | bool | true |
| `show_all_controls` | bool | false (default-open the collapsed section) |
| `toggle_scancode` | integer | 60 = SDL_SCANCODE_F3 |

**Hotkey (F3 by default)** — `SbcOverlayProcessHotkey()`, called from
`xemu_hud_draw()` at `ui/xui/main.cc:302`, in the same block as the F1/F2
menu hotkeys, guarded by `!ImGui::IsWindowFocused(AnyWindow) &&
!g_scene_mgr.IsDisplayingScene()`. It reads the raw scancode via
`SDL_GetKeyboardState()` (NOT an ImGui key), edge-detects, and flips
`enable`. F3 was chosen as verified-free in stock and rebound layouts,
outside the ISS-B12-gated F5–F8 cockpit set (`main.cc:305-315`).

**View menu** — "SBC Cockpit Overlay" item with shortcut label "F3"
(`menubar.cc:240-245`), enabled only while `SbcOverlayAvailable()`; greyed
(not hidden) otherwise so the entry explains itself.

**API** (`sbc-overlay.hh`): `SbcOverlayAvailable()`,
`SbcOverlayProcessHotkey()`, `SbcOverlayDraw()` (called last in
`xemu_hud_draw()`, `main.cc:329`).

**State input** — `xemu_input_get_keyboard_sbc()` (`ui/xemu-input.c:1027`):
the bound controller whose `type == INPUT_DEVICE_SDL_KEYBOARD` and driver is
`DRIVER_STEEL_BATTALION`, else NULL. `xemu_input_keyboard_feeds_sbc()` is a
one-line wrapper, so the predicate has exactly one implementation.

## State & data structures

- `SteelBattalionState` (`ui/xemu-input.h:186-193`), read from
  `ControllerState.sbc`: `buttons` (u64; low bits = SBC_BUTTON_* masks;
  `button >> 32` = the toggle-lamp byteMask), `axis[SBC_AXIS__COUNT]` (i16;
  pedals unipolar 0..32767, aim X/Y + rotation lever bipolar
  -32768..32767 centre-zero), `gearLever` (u8: 254=R, 255=N, 1..5),
  `tunerDial` (u8, drawn %02u, 0-15), `toggleSwitches` (u8 lamp bits).
- **The control table** (`sbc-overlay.cc:41-111`): five static arrays
  (`g_startup`, `g_combat`, `g_toggles`, `g_rest`, `g_axis_keys`) of
  `{label, int *scancode, uint64_t button}`. `scancode` points **directly at
  the generated config field** — the same source `xemu_input.c` populates its
  scancode map from — so labels track rebinds with no extra plumbing; NULL
  scancode = mouse-hardwired (main/sub/lock-on). One table backs three
  consumers: key labels, the "all controls" list, and the toggle-collision
  guard, so the guard cannot drift from the map.
- Static locals: `was_down` (hotkey edge detector), `warned_collision`
  (one-shot notification latch).

## Invariants

1. [VERIFIED] **On-state renders correctly during gameplay** — gear/tuner,
   L/M/R pedal meters, five toggle lamps, startup momentaries, combat row,
   each with its live key label. Verified live in-game 2026-07-26; screenshot
   `docs/features/img/sbc-cockpit-overlay-live.png`.
2. [DERIVED] **Off-state is inert**: with no keyboard-fed SBC, `Draw` and the
   hotkey both early-return (`SbcOverlayDraw` also returns when `enable` is
   false); Duke-bound runs are claimed pixel-identical. The guard is plainly
   in code, but the T8 step-4 Duke A/B was never run on record.
3. [DERIVED] **Never captures input**: window flags `NoMove | NoDecoration |
   AlwaysAutoResize | NoSavedSettings | NoFocusOnAppearing | NoNav |
   NoInputs` (`sbc-overlay.cc:305-309`) — click-through and focus-free,
   because the mouse is the aiming lever (raw `SDL_GetMouseState` in
   `xemu-input.c`).
4. [DERIVED] **Cockpit wins**: if `toggle_scancode` equals any scancode in
   the control table, the hotkey stands down with a one-shot notification;
   the View item and rebinding `toggle_scancode` remain as escape hatches.
   Never deliberately exercised (T8 step 3 not on record).
5. [VERIFIED] **Gear decode is 254=R, 255=N, 1..5** — copied from the
   field-verified switch in `ui/xui/gl-helpers.cc:1016-1038`; an "improved"
   re-derivation was falsified by field evidence and reverted (ISS-B11).
   Do not re-derive.
6. [DERIVED] **Toggle-lamp bit = `(SBC_BUTTON_* >> 32)`** — exactly the
   byteMask `xemu_input.c` flips into `sbc.toggleSwitches`.
7. [DERIVED] **No atomics needed**: writer
   (`xemu_input_update_controllers()`, `ui/xemu.c:982`) and the ImGui read
   run sequentially on the display-loop thread under `xemu_main_loop_lock`.
8. [DERIVED] **Hotkey is swallowed while any ImGui window is focused or a
   scene is displayed** (the `main.cc:274-275` guard) — F3 in a menu does
   nothing.
9. [DERIVED] Invalid/unknown scancodes render as `--`; mouse-bound rows as
   `mouse`; an out-of-range `toggle_scancode` disables the hotkey entirely.

## Policies & decision points

- **Raw SDL scancode, not ImGuiKey, for the toggle** [DERIVED]: keeps the
  key rebindable via one config int and lets the collision guard compare
  identity directly against the SBC scancode map.
- **Corner placement** [DERIVED]: enum bit 0 = right, bit 1 = bottom
  (mirrors the notification recipe); top anchors sit below the fading
  menubar via `g_main_menu_height`.
- **Draw order** [DERIVED]: submitted last in `xemu_hud_draw()` (after
  scene manager, notifications, snapshot manager), and never focused — so it
  overlays them in submission order on the desktop GL HUD
  (`xemu_hud_render()` → `ImGui_ImplOpenGL3_RenderDrawData`).
- **Reuse of `RenderSteelBattalionController` rejected for v1** [VERIFIED
  via feature doc]: no key labels (the point of the feature), unreadable
  below ~350 px, shared-FBO conflict with the Input menu.
- **Sidecar discipline** [VERIFIED via feature doc]: all logic in the new
  MIT-licensed pair; upstream-file diff ~20 lines (main.cc, menubar.cc,
  meson, config block, plus the `xemu_input_get_keyboard_sbc()` accessor).

## Relationship to the VR SBC world panel (cross-reference — do not duplicate)

The in-headset panel (`hw/xbox/nv2a/pgraph/vk/vr_sbc_panel.c`) mirrors this
overlay **row-for-row** but shares no rendering code: it is a hand-rasterized
768x256 RGBA quad composited world-locked by the VR compositor
(`vr_xr_compositor.c:1373`), because the ImGui HUD can never appear in the
headset. Both read the same source, `xemu_input_get_keyboard_sbc()`; the
panel publishes at `ui/xemu.c:987` (inside the main-loop lock, right after
the input update). The desktop overlay's `display.ui.sbc_overlay.*` keys do
**not** control the panel. Panel behavior, threading (seqlock slot), and the
stubbed `vr_sbc_panel_inject()` interaction boundary are specified in
`A1-vr-subsystem.md`; the
shared FIELD-VERIFIED semantics (gear decode, lamp bit, axis ranges) are
normative here (invariants 5-6) and copied there.

## Failure modes

- **Toggle collision** → hotkey silently dead except for one one-shot
  notification per session; users may report "F3 stopped working". [DERIVED]
- **Semantics triplication**: the gear decode + lamp-bit rules now exist in
  three places (`gl-helpers.cc`, `sbc-overlay.cc`, `vr_sbc_panel.c`); an
  edit to one silently desynchronizes the mirrors. [DERIVED]
- **Upstream PR #1803 unmerged**: a future re-port that shifts
  `SteelBattalionState` breaks the overlay; this file pair is the single fix
  site. [ASSUMED]
- **Stale-binary trap**: `display.ui.sbc_overlay.*` comes from config
  codegen — a rebuild that skips codegen (or testing yesterday's binary)
  shows no overlay at all.
- Desktop-mirror only: a user in the headset sees nothing from this overlay
  (by design; the panel covers that).

## Instrumentation

- The one-shot collision notification (`xemu_queue_notification`,
  `sbc-overlay.cc:251-254`) is the only emitted signal.
- No counters or logging; verification is visual — rig screenshot via
  `~/lane4-grab.sh` (composited-root grab; direct window-grabs of GL windows
  are blank).

## Verified-by

- Live in-game verification 2026-07-26 + screenshot
  (`docs/features/sbc-cockpit-overlay.md` header;
  `docs/features/img/sbc-cockpit-overlay-live.png`).
- Built clean as part of the 2026-07-26 perf-stack builds (same doc).
- Gear-decode provenance: ISS-B11 reverted-encoding episode.
- Feature-doc T8 checklist: step 2 (stock layout, F3, live gear glyph)
  covered by the live verification; steps 3-4 (rebind-follow, collision
  stand-down, Duke off-state A/B) **not on record**.

## Confidence & unknowns

- Collision guard (inv. 4) and off-state A/B (inv. 2) never deliberately
  exercised — highest-value cheap experiments to upgrade to [VERIFIED].
- Rebound-layout label tracking believed automatic via the aliased config
  pointers; untested live. [DERIVED]
- Non-default `corner`/`opacity` values and `show_all_controls=true` never
  exercised. [ASSUMED harmless]
- Whether hiding the overlay for a **gamepad-fed** SBC (current gate) is the
  right UX, or whether a stateless-labels variant should show — open (feature
  doc calls gamepad labels "Phase 3").
- `docs/features/sbc-analog-input.md` §6-§10 plans a calibration/diagnostics
  panel in this same UI stack; if built, it must not inherit this overlay's
  `NoInputs` recipe (it needs interaction) — boundary undecided.
