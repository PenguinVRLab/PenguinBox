# Spec — Input stack (XID devices, bindings, SBC)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| Tier | B (inherited xemu, modified by us — SBC hardening, hotkey gating, pedal scaling) |
| Code | `hw/xbox/xid.h`, `hw/xbox/xid.c`, `hw/xbox/xid-gamepad.c`, `hw/xbox/xid-steel-battalion.c`, `ui/xemu-input.{c,h}`, `config_spec.yml` input section, hotkey sites in `ui/xemu.c`, `ui/xui/{main,menubar,main-menu,sbc-overlay,input-manager}.cc`, machine side `system/vl.c` + `hw/xbox/xbox.c` |
| Status | v1 draft |
| Last verified | 2026-07-27 (code reading; tags per claim) |

## Purpose & scope (what it does; what it deliberately does not)

The input stack carries host input (SDL keyboard, SDL gamepads, mouse) into
the guest as emulated Xbox USB **XID** devices — two halves joined by one
struct: the **frontend** (`ui/xemu-input.c`) polls SDL into a per-controller
`ControllerState`; the **device half** (`hw/xbox/xid*.c`) converts that state
into USB reports when the *guest* polls. Three drivers: Duke
(`usb-xbox-gamepad`), Controller S (`usb-xbox-gamepad-s`), Steel Battalion
(`usb-steel-battalion`) — `xid.h:56-58`, aliases `ui/xemu-input.h:35-41`. The
stack also owns port topology, hotplug/rebind, XMU peripheral attach, and the
SBC-vs-global hotkey arbitration (ISS-B12).

Deliberately NOT in scope / not built: USB passthrough (transport linked, no
UI, no config key — policy in `docs/features/sbc-input-plan.md` §7); the
planned merge layer, calibration, diagnostics, pre-flight (plan §§4,6-10 —
**none of it exists in code yet**); VR overlay input injection;
snapshot/loadvm compatibility (spec **B8-snapshots-loadvm.md**, written in
parallel — B6 states only the hotplug fact that causes the failure); SBC
cockpit overlay *rendering* (Tier-A4) — only its hotkey/collision touchpoints
appear here.

## External interface (env flags / config keys / guest-visible USB descriptors / APIs — every way in or out)

**CLI.** `-config_path <file>` replaces the default `xemu.toml` path before
settings load (`ui/xemu.c:1359-1368`). This is the "per-title toml" mechanism:
one file, chosen at launch; there is **no layering** of a global file plus a
per-title file (`ui/xemu-settings.cc:158-207` loads exactly one path).

**Config keys** (all under `input:` in `config_spec.yml` unless noted):
- `bindings.port{1..4}` — device GUID (or literal `"keyboard"`) auto-bound to
  that port (`config_spec.yml:24-32`, `ui/xemu-input.c:99-104,430-446`).
- `bindings.port{1..4}_driver` — driver string per port; only the three
  driver names recognized, anything else silently resolves to Duke
  (`ui/xemu-input.c:248-267`).
- `peripherals.port{1..4}.peripheral_{type,param}_{0,1}` — XMU slots
  (`config_spec.yml:33-53`, maps `ui/xemu-input.c:113-133`).
- `auto_bind` (default true) — newly seen gamepad → first free port
  (`config_spec.yml:55-57`, `ui/xemu-input.c:533-541`); `background_input_capture`
  — sets `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` at init (`ui/xemu-input.c:273-275`).
- `allow_vibration` — deprecated; migrated into per-mapping `enable_rumble`,
  forced true on save (`config_spec.yml:58-61`, `ui/xemu-settings.cc:225-228,279-305`).
- `gamecontrollerdb_path` — **dead key**: generated into the config struct,
  read by no code (repo-wide grep; only `build/xemu-config.h:162,538-539`).
- `keyboard_controller_scancode_map.*` — 25 scancodes, keyboard→gamepad,
  defaults `config_spec.yml:63-139` (a=4 … rtrigger=18); read **live** from
  `g_config` every poll (`ui/xemu-input.c:782-783`).
- `keyboard_sbc_scancode_map.*` — 49 scancodes, keyboard→SBC, defaults
  `config_spec.yml:140-287` (eject=41/Esc … middle_pedal=22/s; toggles on
  F8-F12 at 234-248); copied **once** into a static array at init
  (`ui/xemu-input.c:303-399`).
- `gamepad_mappings[]` — per-GUID SDL button/axis mapping + `enable_rumble` +
  four axis-invert bools (`config_spec.yml:288-372`); a
  `steel_battalion_mapping` block is **commented out, unimplemented**
  (`config_spec.yml:295-296`).
- `display.ui.sbc_overlay.toggle_scancode` (default 60/F3) — overlay hotkey
  (`config_spec.yml:547-548`, `ui/xui/sbc-overlay.cc:230-270`).
- Interacting, owned elsewhere: `general.snapshots.shortcuts.f5..f8`
  (`config_spec.yml:15-21`) — the snapshot hotkeys ISS-B12 gates.

**Guest-visible USB identity.**
- Duke `045E:0202` / Controller S `045E:0289` (`hw/xbox/xid-gamepad.c:81-105`);
  class `0x58` subclass `0x42`, interrupt endpoints `0x82` IN / `0x02` OUT,
  `wMaxPacketSize 0x20`, `bInterval 4` (`xid-gamepad.c:41-62`). XID descriptor:
  type GAMEPAD, subtype 1/2, in-report 20, out-report 6 (`xid-gamepad.c:107-127`).
- SBC `0A7B:D000` (Capcom), endpoints `0x82` IN / `0x01` OUT
  (`hw/xbox/xid-steel-battalion.c:33-34,158-169`); XID type `0x80`, in-report
  26, out-report 22 — both **derived from `sizeof()`** since the D1 fix
  (`xid-steel-battalion.c:171-184`).
- Control requests served: HID `GET_REPORT`/`SET_REPORT`, vendor
  `GET_DESCRIPTOR(0x4200)`, vendor `XID_GET_CAPABILITIES(0x01)`
  (`xid.c:111-216`, `xid-steel-battalion.c:228-345`).

**Host APIs.** `xemu_input_init/bind/update_*`, `xemu_input_get_bound(index)`,
`xemu_input_keyboard_feeds_sbc()` + `xemu_input_get_keyboard_sbc()` (the
ISS-B12 predicate; `ui/xemu-input.h:231-260`), `xemu_input_set_test_mode`.
Devices can also be added from the HMP monitor (`device_add usb-hub,port=1.3`
/ `device_add usb-host,…,port=1.3.1` — passthrough bring-up, plan §2 P0.3).

## State & data structures (the structs that matter and what each FIELD means)

**`ControllerState`** (`ui/xemu-input.h:195-218`) — one per host input device,
on `available_controllers`; indexed by `bound_controllers[4]` when bound.
Holds BOTH payload structs, only the one matching the port's driver ever
populated: `gp` (`GamepadState`, :172-184 — 16-bit `buttons` mask, six int16
`axis[]`, `rumble_l/r` written by the device half) and `sbc`
(`SteelBattalionState`, :186-193). `type` selects keyboard vs SDL-gamepad
polling; `bound` = port index or −1; `device` = qdev opaque of the plugged
USB device (internal hub for Duke/S, the XID itself for SBC —
`ui/xemu-input.c:1152`); `controller_map` points into
`g_config.input.gamepad_mappings[]` (SDL gamepads only; NULL for keyboard).

**`SteelBattalionState`**: `buttons` is a 43-bit host-side mask
(`ui/xemu-input.h:63-109`; bits 0-38 are wire buttons; bits 39-42 —
GEAR_UP/DOWN, TUNER_LEFT/RIGHT — are host-only pseudo-buttons that step the
counters below and never reach the wire). `previousButtons` gives edge
detection. `toggleSwitches` = **latched** level of the five cockpit toggles
(bits `SBC_BUTTON_x >> 32`, mask `SBC_TOGGLE_BITS_HI 0x7C`,
`ui/xemu-input.h:111-114`). `gearLever`: 254=R, 255=N, 1..5 (0 only at
power-on, promoted to 255 on first refresh, `ui/xemu-input.c:642-643,851-852`).
`tunerDial` 0..15, wraps. `axis[8]` int16, but the three pedal slots hold
**unsigned 0..0xFFFF bit-cast** values (`SBC_PEDAL_FULL = (int16_t)0xFFFF`,
`ui/xemu-input.h:116-130`, D3 fix).

**Wire reports.** `XIDGamepadReport` (`hw/xbox/xid.h:99-108`, 20 bytes):
`wButtons` holds the 8 binary buttons via `BUTTON_MASK` (`xid.h:69-78`);
A/B/X/Y/Black/White are **analog bytes** 0/0xFF in `bAnalogButtons[8]`;
triggers bytes (host int16 ≫7, `xid.c:98-99`); sticks int16. GUIDE (bit 14)
is HUD-only — never mapped to the wire (`xid.c:66-84`).
`XIDSteelBattalionReport` (`hw/xbox/xid-steel-battalion.c:36-55`, 26 bytes):
`dwButtons` = host bits 0-31; `bMoreButtons` = bits 32-38 (COM5, SightChange,
five toggles — toggle bits carry the **latched** level, momentary presses
masked, `:201-209`, D5 fix); `bAimingX/Y` uint8 = 128 + axis/256 (`:213-218`);
`sRotationLever`, `sSightChangeX/Y` int16 (guest uses high byte); three uint16
pedals; `ucTunerDial` low nibble; `ucGearLever`. The 22-byte output report
(`:59-101`) is the LED panel — accepted, stored, **consumed by nothing**
(`:277` `// update_output(s);`, TODO `:376-378`).

**Device states.** `USBXIDGamepadState` (`xid.h:117-126`) /
`USBXIDSteelBattalionState` (`xid-steel-battalion.c:103-112`): `in_state`
(bLength/length set from `sizeof()` at realize — `xid-gamepad.c:178-182`,
`xid-steel-battalion.c:409-413`), `out_state`, 0xFF-filled `_capabilities`
twins, and `device_index` — the **qdev property `index`**
(`xid-gamepad.c:219-221`, `xid-steel-battalion.c:426-428`) linking the device
back to `bound_controllers[]` via `xemu_input_get_bound()`.

**Frontend globals** (`ui/xemu-input.c:91-135`): `bound_controllers[4]`;
`bound_drivers[4]` (defaults Duke, loaded from config at init :412-415);
`available_controllers`; `sdl_sbc_kbd_scancode_map[52]` — the init-time SBC
snapshot (indices 3-42 buttons, 43-51 axis keys, 0-2 never populated: those
controls are mouse-hardwired); `g_keyboard_scancode_map[25]` (pointers into
live `g_config`); `port_map[4] = {3,4,1,2}` (:269).

## Invariants (numbered I1..In; testable statements; each tagged)

- **I1** Guest input is **pull-only**: reports are built when the guest polls
  (interrupt IN or `GET_REPORT`) via `update_input`/`update_sbc_input` →
  `xemu_input_update_controller` → cached-SDL reads; no event-driven push
  path exists. (`xid.c:126-140`, `xid-gamepad.c:136-144`,
  `xid-steel-battalion.c:247-261,356-365`) [DERIVED]
- **I2** `ControllerState.gp` and `.sbc` are zeroed at the top of every
  refresh and only the branch matching the bound port's driver repopulates
  one of them; the other stays zero for that frame.
  (`ui/xemu-input.c:629-640,828-837`) [DERIVED]
- **I3** A device is bound to at most one port and a port holds at most one
  device; `xemu_input_bind` first unbinds any occupant of the target port and
  any prior binding of the incoming device. (`ui/xemu-input.c:1050-1075,
  1093-1100`) [DERIVED]
- **I4** State refresh and rumble transmission are rate-limited to one per
  2500 µs per controller; faster guest polls see the previous (stale but
  consistent) frame. (`ui/xemu-input.c:52-53,601-604,1012-1015`) [DERIVED]
- **I5** Config precedence is per-key replacement: compiled-in defaults from
  `config_spec.yml`, overwritten by whichever single toml is loaded
  (`config_tree.update_from_table`, `ui/xemu-settings.cc:180,204`); saves
  write only the delta from defaults (`:231`). A default and an override for
  the same key never coexist at runtime. [DERIVED]
- **I6** **Keyboard-map binding is driver-selected, not precedence-merged.**
  Exactly one of the two maps is consulted per refresh, chosen by
  `get_bound_driver(state->bound)`: the SBC map iff the port's driver is
  `usb-steel-battalion`, else the gamepad map (`ui/xemu-input.c:639-640,780`).
  A scancode present in both maps drives only the selected map's control; the
  other **cannot fire** (its struct zeroed by I2, its map never read). [DERIVED]
- **I7** Within ONE map, N controls sharing a scancode **all fire
  simultaneously** — every control is an independent read of the same
  `SDL_GetKeyboardState` array with no consumption or exclusivity
  (`ui/xemu-input.c:646-649,785-821`). [DERIVED]
- **I8** The SBC keyboard map is a **one-shot snapshot**: config edits after
  `xemu_input_init` (:303-399) do not change SBC input until restart, while
  gamepad-map edits apply on the next poll (live reads, :782-783). The SBC
  overlay's key labels read the live config (`ui/xui/sbc-overlay.cc:28-30`),
  so a runtime rebind would desync labels from behavior; no runtime writer
  exists today, so they agree. [DERIVED]
- **I9** XID controllers (and their per-controller hubs) exist **only via
  hotplug**: created by `qdev_device_add` from the UI thread
  (`ui/xemu-input.c:1106-1152`), no earlier than `xemu_input_init`
  (`ui/xemu.c:1409`) — after machine creation. Only the 4-port
  "daughterboard" root hub is machine-init (`system/vl.c:3100-3102`).
  Consequence: CLI `-loadvm` (`system/vl.c:2882-2885`) restores into a
  machine with **no XID devices** → `Unknown section or instance
  '...usb-steel-battalion'`. Ordering details and the working
  monitor-loadvm procedure: **spec B8**. [VERIFIED — field failure, SB
  campaign; mechanism traced in code]
- **I10** Port driver strings outside the three known values resolve silently
  to Duke (`ui/xemu-input.c:248-267`), and a saved rebind writes the resolved
  value back to config (`:1087-1089`) — the plan's F2 defect, still live.
  [DERIVED]
- **I11** The five SBC toggles report **latched level only**: rising edges XOR
  `toggleSwitches` (`ui/xemu-input.c:651-665,860-874`), and the report masks
  the momentary bits with `~SBC_TOGGLE_BITS_HI`
  (`xid-steel-battalion.c:201-209`). [DERIVED]
- **I12** Gear (`{254,255,1..5}`) and tuner (`0..15`, wrapping) are
  edge-stepped counters; no absolute-position input path exists.
  (`ui/xemu-input.c:667-705,876-914`) [DERIVED]
- **I13** On the SBC device every guest-controllable length is clamped to
  host `sizeof()` before any copy and every unexpected request STALLs
  (D2/D4 — `xid-steel-battalion.c:271-276,288-292,337-343,366-379`). The
  **gamepad** device retains guest-reachable asserts
  (`xid.c:138,149,158,165,170,189,213`; `xid-gamepad.c:142,150,155`). [DERIVED]
- **I14** `test_mode` (HUD controller-nav capture,
  `ui/xui/input-manager.cc:50-59`) freezes the **last built guest report** on
  all XID devices — update functions return early (`xid.c:43-46,57-60`,
  `xid-steel-battalion.c:188-191`); a button held when the HUD opened stays
  held from the guest's view. [DERIVED]
- **I15** Rumble flows only to SDL gamepads whose mapping has `enable_rumble`
  (`ui/xemu-input.c:1002-1020`); the SBC never generates rumble (its OUT
  report is LEDs, unconsumed), and keyboard controllers return early. [DERIVED]

## Policies & decision points (every branch that chooses a path — precedence, fallbacks — and why)

**P1 — Port assignment.** At startup the keyboard binds only if some
`bindings.portN == "keyboard"` (`ui/xemu-input.c:417-425`; no auto-bind
fallback). Each SDL gamepad, on `SDL_EVENT_GAMEPAD_ADDED` (fired for
already-present pads at init): (a) first *unoccupied* config port whose GUID
matches — duplicates tolerated for X360 wireless receivers (`:514-530`); (b)
else if `auto_bind`, first free port, and this binding is saved (`:533-541`);
else unbound. Removal unbinds without saving so reconnect restores the port
(`:560-571`).

**P2 — Driver per port.** `get_bound_driver()` maps the config string to one
of three drivers with the silent Duke fallback (I10). The HUD "Emulated
Device" combo writes `bound_drivers[active]` and immediately rebinds with
save=1 (`ui/xui/main-menu.cc:217-241`) — a live `qdev_unplug` + re-add of the
port. The write-back exists for auto-reconnect; its cost is laundering the
fallback into config (F2).

**P3 — Keyboard map selection (the Q1 answer).** Selection is I6's driver
branch. Field case: default `rtrigger = 18` (`config_spec.yml:137-139`) plus
a per-title toml overriding `oxygen_supply_system = 18` (default 66/F9,
`config_spec.yml:237-239`). After load both keys hold 18 — in **different
maps**. Keyboard on an SBC port: scancode 18 ('o') sets host bit 35 →
XOR-flips the OXY toggle; `rtrigger` cannot fire (gamepad map unread, `gp`
zeroed). Keyboard on a Duke/S port: the same key drives RTRIG to 32767
(`ui/xemu-input.c:820-821`); oxygen never fires. **BOTH never fire**: exactly
one keyboard `ControllerState` exists (`:283-291`), bound to at most one port
(I3). The collision that burned us is a *mode flip*, not a double-fire — the
key's meaning follows the port's driver, so an unnoticed Duke fallback (I10)
silently turns an oxygen binding into a right-trigger. Amplifier:
`config_spec.yml:139` mis-comments 18 as `# w` — 18 is **'o'**, 26 is 'w'
(`subprojects/SDL3-3.4.10/include/SDL3/SDL_scancode.h:77,85`). A true
double-fire needs two controls in the **same** map on one scancode (I7).

**P4 — SBC keyboard specials.** Buttons come from map indices 3-42; bits 0-2
(MainWeapon/SubWeapon/LockOn) are **mouse-hardwired** — left, X1/X2, right
button (`ui/xemu-input.c:711-720`) — and aiming X/Y track the mouse through
the viewport to ±32767 (`:722-756`); no config keys exist for bits 0-2.
Sight-change, rotation and pedal keys write full-scale axis values (`:758-777`).

**P5 — SBC on a real SDL gamepad.** A **hardcoded** 8-entry map (South=Main
weapon, East=Lock-on, LB=Func1, LS-click=SightChange, dpad=gear/tuner —
`ui/xemu-input.c:840-849`), sticks→sight-change/aiming, triggers→middle/right
pedal via `sbc_pedal_from_trigger` (`:919-932`). Left pedal and every other
SBC button are **unreachable** from a gamepad; neither the per-GUID
`controller_map` nor the yml's commented `steel_battalion_mapping` is
consulted. Keyboard is the only complete SBC source today.

**P6 — Duke/S analog policy.** Buttons/axes resolve through the per-GUID
mapping; stick Y axes are inverted **by default** (SDL y-down → Xbox y-up:
the `invert_axis_*_y=false` path applies `-1-v`, `ui/xemu-input.c:977-996`).
**No deadzones anywhere in the guest path** — raw SDL values pass through
(the 8000 thumb deadzone in `ui/xui/input-manager.cc:65` is HUD navigation
only). Triggers: SDL 0..32767 → byte via ≫7 (`xid.c:98-99`). SBC path: **no
inversion, no deadzone**; pedals scale 0..32767 → 0..0xFFFF (D3 fix,
`ui/xemu-input.h:122-130`).

**P7 — Topology.** Machine init: two 4-port OHCI controllers
(`hw/xbox/xbox.c:317-323`) plus the daughterboard hub at root port 1
(`system/vl.c:3100-3102`) giving ports `1.1..1.4`. Player index → physical
port via `port_map {3,4,1,2}` (player 1 = `1.3`). Duke/S get a 3-port
internal hub at `1.<pm>`, XID at `1.<pm>.1`, XMU slots at `1.<pm>.2/.3`
(`ui/xemu-input.c:1106-1152,1209,1238-1240`); the SBC plugs **directly** at
`1.<pm>` — no hub, hence **no XMU slots** (asserted `:1162-1164`; HUD hides
the section, `ui/xui/main-menu.cc:379-382`).

**P8 — Hotkey arbitration (ISS-B12): the cockpit wins.**
`xemu_input_keyboard_feeds_sbc()` (true iff some bound port runs the SBC
driver fed by the keyboard, `ui/xemu-input.c:1027-1042`) gates: F5-F8
snapshot shortcuts (`ui/xui/main.cc:304-315`), F10 renderdoc / F11 fullscreen
/ F12 screenshot (`ui/xui/menubar.cc:75-95`), bare F8/F9 VR recenter /
RAM-hunt dump (`ui/xemu.c:927-941`). **Ctrl+F8 always recenters** (escape
hatch, `ui/xemu.c:909-926`) — and since the SBC path is a raw un-consuming
keyboard read, the chord also delivers F8 to the cockpit (stock layout: flips
FILT once; accepted). A non-keyboard-fed SBC leaves all globals live. The
overlay toggle (default F3) self-disables with a one-shot warning if its
scancode collides with any SBC binding (`ui/xui/sbc-overlay.cc:230-258`).

## Failure modes (what breaks when invariants are violated; known asserts; traps already hit)

- **CLI `-loadvm` with any controller bound** → `Unknown section or instance
  '...usb-steel-battalion'` (or the gamepad type name): I9, hit in the field
  during the SB snapshot-measurement workflow. Load via monitor after the UI
  has plugged devices — procedure and instance-id caveats in **B8** (also
  plan F3: a changed port driver makes old snapshots unloadable mid-stream).
- **Guest-reachable aborts on the gamepad path** (I13): malformed SET_REPORT
  length, unknown control request, or unexpected endpoint assert-crash the
  emulator (`xid.c:149,158,213`, `xid-gamepad.c:142,150`). Also
  `update_input`/`update_output` `assert(state)` on an unbound port
  (`xid.c:48-49,62-63`) — an unplug race a guest poll can hit; the SBC path
  tolerates it (`xid-steel-battalion.c:193-198`, R7), the Duke path does not.
- **Silent driver corruption** (I10/P2): two builds sharing a config, or a
  typo'd `portN_driver`, silently becomes Duke and is written back on the
  next saved rebind — an SBC per-title setup degrades to a gamepad with no
  message, and every SBC key reinterprets under the gamepad map (the
  scancode-18 mode flip, P3).
- **Same-map double-fire** (I7): rebinding a key already used in that map
  drives both controls with no warning (only the overlay's own toggle key has
  a collision guard).
- **Pedal scancodes escape sanitize**: the SBC range-check loop covers
  indices 0-48 only (`ui/xemu-input.c:401-410`); pedals live at 49-51
  (`:394-399`) — an out-of-range `left/right/middle_pedal` toml value indexes
  the 512-entry SDL key array out of bounds every poll (OOB read / UB). The
  25 gamepad keys and SBC indices 3-48 are clamped (`:293-301,401-410`).
- **HUD keyboard leak**: `test_mode` is set only while ImGui `NavActive`
  (`ui/xui/input-manager.cc:50-59`); typing into HUD text fields with nav
  inactive still drives the emulated pad — state polling ignores the HUD
  keyboard-capture flag, which gates only SDL *events* (`ui/xemu.c:895-908`).
  Conversely test_mode's freeze (I14) holds a stale button down — worse
  mid-ignition on an SBC (plan §6 R8).
- **Shift+F8 dual-fire** (non-SBC mode): VR recenter excludes only CTRL/ALT
  (`ui/xemu.c:933-934`) while Shift+F8 is "save snapshot slot 4"
  (`ui/xui/main.cc:308-311`) — both fire, as recorded in plan §0.
- **"PRESS MAIN WEAPON BUTTON" dead-end** (field): SBC bits 0-2 have no
  keyboard binding — a usable mouse is mandatory (P4); from a bare gamepad
  most SBC controls, including Start/Ignition, are unreachable (P5).
- **Docs-vs-code discrepancies (code wins):** `sbc-input-plan.md` §8 lists
  D1-D5 as live defects — **all five are fixed in tree**, comments citing the
  IDs (`xid-steel-battalion.c:177-184` D1, `:258-343` D2, `:366-379` D4,
  `:201-209` D5; `ui/xemu-input.h:116-130` + `ui/xemu-input.c:772-777,929-932`
  D3). `sbc-analog-input.md` §1 claims xemu pedals "default to max 0xFFFF" —
  false at HEAD: axes are zeroed each refresh (I2), pedals idle at 0 (the
  trap is adapter/real-hardware lore), and its `0xFF00` full scale disagrees
  with code (full scale 0xFFFF; guest reads high byte 0xFF). Plan §3's F1
  "hotkey arbiter" was **not** built as specified — the shipped mechanism is
  predicate gating at five sites (P8); F2/F4 remain unimplemented.

## Instrumentation (how to observe this feature from a log/monitor)

- Compile-time: `DEBUG_INPUT` (`ui/xemu-input.c:42`) traces
  hotplug/open/remove; `DEBUG_XID` (`xid.h:36`, `xid-gamepad.c:24`,
  `xid-steel-battalion.c:24`) traces every guest control/data transfer — the
  direct way to watch guest poll cadence (plan P0.4's open question).
- Startup stderr: active config path (`ui/xemu-settings.cc:70,103` — which
  per-title toml loaded); out-of-range scancode warnings
  (`ui/xemu-input.c:294-300,404-408`); launch-parameter echo showing the
  daughterboard hub (`system/vl.c:3110-3114`).
- HUD notifications: `Connected '<name>' to port N` / `Port N disconnected`
  (`ui/xemu-input.c:420-423,545-546,564-566`), XMU attach/errors, the
  overlay's toggle-collision warning.
- Live state: the SBC cockpit overlay (F3) mirrors `sbc.*` exactly as the
  guest reads it (`ui/xui/sbc-overlay.cc`); `vr_sbc_panel_publish()`
  snapshots it per frame (`ui/xemu.c:983-988`). HMP monitor (`~`): `info usb`
  shows topology and hotplugged devices.

## Verified-by (experiments/gates backing claims; cite runnable commands where they exist)

No dedicated input gates exist yet; this v1 is code-derived. Standing
evidence: (a) the D1-D5 in-tree fixes and comments — each encodes a defect
independently confirmed in the 2026-07-25 adversarial pass
(`sbc-input-plan.md` §10); (b) ISS-B12's fix entry (`KNOWN_ISSUES.md`),
marked **rig-verify pending**; (c) the field `-loadvm` failure (I9),
reproducible whenever a snapshot taken with a bound controller is loaded from
CLI. Cheap future falsifiers: bind keyboard to an SBC port, set
`oxygen_supply_system = 18`, press 'o' with the overlay open — OXY must flip
and RTRIG stay 0 (P3/I6); duplicate a scancode within one map and observe
both controls fire (I7); set `left_pedal = 9999` under ASan to expose the
unsanitized OOB read; DEBUG_XID call counting settles poll cadence.

## Confidence & unknowns (open questions; repeat every [ASSUMED] claim here)

Most claims above are [DERIVED] — read and traced, not exercised; that is the
honest state of a v1. Open items:

- **[ASSUMED] Guest poll cadence**: the guest honors `bInterval 4` (~250
  polls/s). Never measured (plan P0.4); with the 2500 µs limiter (I4) the
  effective input rate is min(guest rate, 400 Hz, UI pump rate).
- **[ASSUMED] Hot driver-switch visibility**: a mid-game driver switch
  replugs correctly at qdev level, but Steel Battalion is believed to
  enumerate controllers only at boot, so a late switch may never be seen
  (plan F4; suspected "no input at the menu" cause). Untested — **runnable
  falsifier now specified**: `docs/features/sbc-input-plan.md` §9 item 6,
  Test B (no controller needed). Note that spec item also distinguishes
  *device-level* swap (qdev replug, the thing this assumption is about) from
  *source-level* swap (swapping which frame source feeds an already-plugged
  emulated device), which the guest cannot observe at all — only the former
  is at risk here.
- **[ASSUMED] SBC axis sign conventions** (sight-change/rotation/mouse-aim
  direction) match real hardware; only pedals and report *widths* were
  checked against external dumps. Aiming low bytes and the tuner upper
  nibble are undocumented globally (plan §8).
- **[ASSUMED] HUD keyboard-leak boundary**: the exact ImGui conditions under
  which `NavActive` is false while a text field consumes keys (the HUD-leak
  failure mode) — the leak is traced from code, the trigger set is not.
- **[ASSUMED] toml unknown-key behavior**: keys absent from
  `config_spec.yml` are silently ignored by `update_from_table` (not traced
  into the generator) — a typo'd override key would fail silent, compounding
  the P3 mode-flip trap.
- Open: no SBC gamepad mapping config (P5) — the commented
  `steel_battalion_mapping` block is the intended fix; the plan-§4 merge
  layer supersedes I2/I11/I12 when it lands and will require a B6 rev.
