# C2 — UI layer component cards (Tier C)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

**Scope:** the SDL + Dear ImGui frontend ("xui") — `ui/xui/*.cc` (24 files) plus
the `ui/` core that hosts it (`ui/xemu.c`, `ui/xemu-monitor.c`, snapshot/thumbnail
glue) and the display-path seam into the NV2A Vulkan renderer.
**What a Tier-C card is** (per `00-specification-plan.md` §1/§3): a one-page map of a
subsystem we *operate but rarely modify* — purpose, interfaces, the invariants we
lean on, and where the danger is. Not a behavioral contract.
**Escalation rule:** the moment a component here is *modified*, its card must be
upgraded to a Tier-B as-built spec **in the same change** (plan §1).
**Tag legend** (plan §3): `[VERIFIED]` = backed by a cited field test/runbook/gate;
`[DERIVED]` = read from code, traced, not deliberately exercised; `[ASSUMED]` =
believed, unproven — a flagged landmine. Untagged statements are plain code facts
(file:line cited).

Surveyed 2026-07-27 against the working tree. The specs cross-referenced
below were written in the same v1 pass and all exist; the index is
[SPECS.md](SPECS.md).

---

### Card 1 — SDL main loop + display path (ui/xemu.c → NV2A VK → GL window)

**Purpose.** Owns `main()`, the SDL3 window, the OpenGL 4.0 core context, the event
loop, and the presentation of the guest frame: the NV2A frame is rendered by the
**Vulkan** backend offscreen, then handed to this GL window through an
external-memory interop — this card is the map of that seam.

**Interfaces.**
- `main()` (`ui/xemu.c:1320`) **is the display thread**. It creates the window +
  GL context (`display_very_early_init`), calls `nv2a_context_init()` (which also
  creates the renderer's offscreen GL context), spawns the `qemu_main` thread
  (`qemu_init` + `qemu_main_loop`), then loops
  `while (!qemu_exiting) { poll_events(scon); gl_render_frame(scon); }` (:1413).
- vblank source: dedicated `vblank-timer` thread at `vblank_interval_ns` =
  16.666 ms (`:77-78`, `use_vblank_timer_thread = true`; QEMUTimer fallback
  exists). It takes `xemu_main_loop_lock()` (main-loop mutex + BQL) and calls
  `graphic_hw_update()` — guest vblank pacing is **not** tied to window swaps.
- Frame acquisition: `gl_render_frame` (`:808`) → `nv2a_get_framebuffer_surface()`
  (`pgraph/pgraph.c:380`, takes `renderer_lock`, asserts `!framebuffer_in_use`) →
  `pgraph_vk_get_framebuffer_surface()` (`vk/renderer.c:321`): looks up the scanout
  surface at `pcrtc.start + line_offset` in the surface cache, sets
  `sync_pending`, `pfifo_kick()`, and **blocks on `sync_complete`**. The pfifo
  thread services it in `pgraph_vk_sync()` (`vk/renderer.c:118`): composite
  (`render_display`) + `xemu_vr_frame()`. Returns a **GL texture id**.
- The interop (`vk/display.c:711-864`): display image is a
  `VK_FORMAT_R8G8B8A8_UNORM` `VkImage` created with
  `VkExternalMemoryImageCreateInfo` (OPAQUE_FD on Linux / OPAQUE_WIN32 on
  Windows), memory allocated with `VkExportMemoryAllocateInfo`, exported via
  `vkGetMemoryFdKHR`, imported into GL with `glCreateMemoryObjectsEXT` +
  `glImportMemoryFdEXT` + `glTexStorageMem2DEXT` (tiling probed via
  `GL_TILING_TYPES_EXT`; optimal unless linear is advertised).
  `HAVE_EXTERNAL_MEMORY` is **hardcoded 1** (`vk/renderer.h:44`).
- Fallback when no scanout surface exists (`tex == 0`, e.g. guest software
  blitting / pre-GPU boot): upload the QEMU `DisplaySurface` pixels to a GL
  texture via the VGA path (`xb_surface_gl_create_texture`, `:637`).
- HUD: `xemu_hud_update()` runs **under BQL** (`:854-856`); `xemu_hud_render()` +
  `glFinish()` + `SDL_GL_SwapWindow` run outside it (deliberate: vsync/occluded
  swap must not stall emulation — see the Wayland hint comment `:995-1001`).

**Invariants we rely on.**
1. **There is no VK↔GL semaphore anywhere — the contract is CPU-complete before
   GL samples** [DERIVED, stated in-code `vk/renderer.c:349-360`]. Sync path: the
   composite finishes on pfifo before `sync_complete` is set. Async-present path
   (S2): composite submitted without wait; the display thread **fence-gates** on
   `present_published_fence` before returning the slot's GL id
   (`vk/renderer.c:361-370`).
2. The offscreen NV2A GL context is created with
   `SDL_GL_SHARE_WITH_CURRENT_CONTEXT=1` against a hidden SDL window
   (`pgraph/thirdparty/gloffscreen/sdl.c:52-70`) — shared namespace, so texture
   ids imported on the pfifo thread are drawable by the window context [DERIVED].
3. `gl_render_frame` is reentry-guarded (`qatomic_xchg` `:810`) because an SDL
   event-watch also invokes it during window resize/drag (`:878-888`).
4. `framebuffer_in_use` never nests (assert `pgraph.c:387`); release broadcasts
   `framebuffer_released`.
5. **Window swap rate ≠ guest fps.** The window loop free-runs at vsync; guest
   fps is the NV2A flip rate (`g_nv2a_stats.increment_fps`) [VERIFIED —
   `runbooks/rig-perf-measurement.md` "Seeing FPS on screen"].

**Danger notes.**
- **Requires NVIDIA GL external-memory interop; will not run under
  Xvfb/llvmpipe** [VERIFIED — AGENTS.md §5; headless smoke tests use
  `renderer = NULL`/OPENGL]. The opaque-FD import is same-device: with VR on, XR
  pins Vulkan to the NVIDIA GPU, so `__NV_PRIME_RENDER_OFFLOAD=1
  __GLX_VENDOR_LIBRARY_NAME=nvidia` is mandatory or the import has no matching
  GL device (hard rule H-6) [VERIFIED — AGENTS.md].
- **MangoHud misattributes on PRIME rigs** [VERIFIED —
  `runbooks/rig-perf-measurement.md` "Seeing FPS on screen"]: it hooks a
  spurious offscreen GL context (the hidden gloffscreen window is the obvious
  candidate [ASSUMED]) and reports garbage (e.g. 1272 fps on the wrong GPU);
  xemu presents via GL while NV2A renders offscreen in Vulkan, so no GL swap
  rate equals guest fps anyway. Use Debug→Video (Card 3), never MangoHud.
- The `sync_pending`/`sync_complete` handshake means a wedged pfifo thread
  freezes the UI thread too [DERIVED].
- GL windows are blank in their own X pixmap — screenshots of the live window
  must grab the composited root (`~/lane4-grab.sh`) [VERIFIED — runbook trap 3].

**Escalation trigger.** Any modification here (threading, interop, swap/vsync,
vblank pacing, fallback path) → upgrade to a Tier-B spec first. Note the async
present ring in `vk/display.c` is **already fork-modified Tier-A/B territory**
(overlap-present-path §5.1) — treat the seam as load-bearing.

---

### Card 2 — Scene manager + scenes (welcome, main-menu, popup-menu, animations)

**Purpose.** Full-screen modal UI flows ("scenes") that sit above the game:
the tabbed settings menu, the controller-first popup ring, the first-boot
welcome window, and the animation primitives they share.

**Interfaces.**
- `Scene` base (`scene.hh`): `Show/Hide/IsAnimating/Draw`; `Draw()` returns
  false when the scene has fully exited.
- `SceneManager g_scene_mgr` (`scene-manager.cc`): `PushScene()` queues;
  `Draw()` runs the single active scene until finished, then activates the next
  queued one. `IsDisplayingScene()` = active or queued.
- Entry points (`main.cc:290-298`): F1 → `g_main_menu`, F2 / controller GUIDE /
  BACK+START → `g_popup_menu`, double-click → fullscreen toggle. These wake-keys
  are gated on **no ImGui window focused and no scene displayed**.
- `MainMenuScene` (`main-menu.cc`, 2010 lines — the big one): 8 tabbed views
  (General/Input/Display/Audio/Network/Snapshots/System/About), LB/RB tab
  switching, `ShowSettings/ShowSnapshots/ShowSystem/ShowAbout` deep links (also
  used by menubar + welcome). Persists `general.last_viewed_menu_index`.
- `PopupMenuScene` (`popup-menu.cc`): menu stack (root → Settings/Games
  submenus); root items: Pause/Resume, Screenshot, Save Snapshot, Games, Eject,
  Load Disc, Settings, Restart, Exit.
- `FirstBootWindow` (`welcome.cc`): **not a Scene** — a plain always-centered
  window gated by `g_config.general.show_welcome`; links to Settings→System.
  While it is open, the guest framebuffer is **not** rendered (`main.cc:208`) and
  notifications are suppressed (`main.cc:327`).
- `animation.cc`: `Animation` / `EasingAnimation` (ease-in/out state machines);
  honor `display.ui.use_animations` — when off they snap to the end state.
  `scene-components.cc`: BackgroundGradient, NavControl hints, BigStateIcon,
  TitleInfo — shared scene furniture.

**Invariants we rely on.**
1. At most one scene is active; queued scenes wait [DERIVED, `scene-manager.cc:38`].
2. Scenes draw inside `xemu_hud_update()` — i.e. **with BQL held** (Card 1), so
   view code freely calls QEMU-side functions (vm_stop, snapshot list, etc.).
3. While a scene is displayed, controller input is captured for navigation and
   withheld from the guest (Card 6 test-mode) [DERIVED].
4. Hide() is animation-driven: a scene keeps drawing until its ease-out ends.

**Danger notes.** Scene `Draw()` returning `true` forever (e.g. an animation that
never completes because `use_animations` toggled mid-flight) would wedge the UI in
that scene [ASSUMED — never observed]. The main-menu views write directly into
`g_config` as they draw; there is no apply/cancel — every widget commit is
immediate and auto-saved on exit.

**Escalation trigger.** Modifying scene lifecycle, input gating, or adding a
scene → Tier-B spec for the scene manager first.

---

### Card 3 — Menubar + Debug menu (incl. the FPS/MSPF instrument)

**Purpose.** The auto-hiding top menu bar (mouse-wake), its global keyboard
shortcuts, and the Debug windows — of which **Debug→Video is the canonical
on-screen FPS/MSPF instrument** for all perf work.

**Interfaces.**
- `ShowMainMenu()` (`menubar.cc:98`): Machine (pause, screenshot, Snapshot
  submenu with the four F5–F8 slots, eject/load disc, Settings, reset, exit) ·
  View (UI scale, **Backend combo → `g_config.display.renderer`**, internal
  resolution scale → `nv2a_set_surface_scale_factor` (live), fit/filter/aspect,
  fullscreen F11, **Recenter VR View Ctrl+F8**, **SBC Cockpit Overlay F3**) ·
  Debug (Monitor `~`, Audio, Video, RenderDoc capture when built) · Help (docs
  URL, Report Compatibility, Check for Updates (Windows), About).
- Auto-hide (`main.cc:231-262`): drawn only if `display.ui.show_menubar` and the
  welcome window is closed; hides after **5 s** without mouse movement (1 s fade
  when animations on). `g_input_mgr.MouseMoved()` (movement or any button held)
  wakes it. `g_main_menu_height` goes to 0 when hidden — notifications and the
  SBC overlay position themselves under it.
- `ProcessKeyboardShortcuts()` (`menubar.cc:49`, every frame): Ctrl/Cmd+E/O/P/R/Q
  = eject/open/pause/reset/quit; `~` toggles the Monitor (Card 5); F12
  screenshot, F11 fullscreen, F10 RenderDoc — **all three disabled while the
  keyboard drives a Steel Battalion controller** (ISS-B12: F10–F12 are cockpit
  keys; menu items remain).
- **Debug→Video** (`debug.cc:346`, `DebugVideoWindow`): ImPlot scrolling plot of
  `FPS: N` = `g_nv2a_stats.increment_fps` — **the true NV2A flip rate = true
  guest fps** (computed in `pgraph/profile.c:36`) — plus `MSPF` (NV2A render ms
  from `g_nv2a_stats.frame_history`), and an Advanced tree plotting every
  nv2a-prof counter per frame (log-Y). Window pos/size/transparency persist in
  `display.debug.video.*`; middle-click toggles transparency. FPS points
  accumulate only while the VM runs. Cross-ref `docs/internal/A3-instrumentation.md`
  for the counters themselves.
- Debug→Audio (`DebugApuWindow`): 256-voice grid; hovering a voice **isolates it
  audibly** (`mcpx_apu_debug_isolate_voice`), right-click mutes; monitor-point
  combo, DSP JIT / GP / EP toggles, utilization + throttle readouts.

**Invariants we rely on.**
1. Wake-the-bar-with-the-mouse → Debug (3rd menu) → Video is the measurement
   procedure; **the overlay persists across loadvm** [VERIFIED —
   `runbooks/rig-perf-measurement.md` "Seeing FPS on screen"].
2. A live overlay reading during real gameplay outranks any log scrape
   [VERIFIED — runbook traps 1–3].
3. `FPS` is flip-rate, not window rate; `~` is the text console, not FPS
   [VERIFIED — runbook].

**Danger notes.** The View→Backend combo writes config immediately but there is
no live renderer switch — it takes effect on next boot [DERIVED: renderer is
chosen once in `pgraph_init_thread`]; combined with H-3 (failed VK init silently
falls back to OPENGL with only a notification), a wrong reading of this combo can
mask a fallback. Debug→Audio's hover-isolation changes what you *hear* while the
window is merely open under the cursor.

**Escalation trigger.** Changing shortcut routing, auto-hide, or anything in
`DebugVideoWindow` (it is measurement infrastructure — A3's display surface) →
Tier-B first.

---

### Card 4 — Snapshot-manager UI

**Purpose.** The UI face of savevm/loadvm: the snapshots list in the main menu,
the F5–F8 bound slots, and the disc-mismatch guard dialog. (Semantics of the
underlying snapshot machinery: `docs/internal/B8-snapshots-loadvm.md`.)

**Interfaces.**
- `SnapshotManager g_snapshot_mgr` (`snapshot-manager.cc`): `Refresh()` wraps
  `xemu_snapshots_list()` (error latches `m_load_failed` so a broken image
  doesn't error-spam every frame). `LoadSnapshotChecked(name)`: compares the
  snapshot's recorded `disc_path` against the currently mounted disc — on
  mismatch opens the **"DVD Drive Image" modal** (Yes = eject + mount recorded
  disc, then loadvm; No = loadvm anyway; Cancel); snapshot-with-no-disc ejects
  first. `Draw()` is called every HUD frame (dialog host).
- Bound slots: `g_snapshot_shortcut_index_key_map` (`ui/xemu-snapshots.c:46`)
  maps F5–F8 → `general.snapshots.shortcuts.f5..f8` (snapshot names in config).
  F5–F8 load / Shift+F5–F8 save (`main.cc:307-315`) — **disabled while the
  keyboard drives an SBC** (ISS-B12; the Machine→Snapshot submenu remains).
  `ActionActivateBoundSnapshot` (`actions.cc:94`) notifies on unbound slots.
- Main-menu Snapshots view (`main-menu.cc:1197-1560`): list with thumbnails,
  regex search, filter-by-current-title (XBE title id), create, per-snapshot
  context menu. Thumbnails: `xemu_snapshots_set_framebuffer_texture()` is fed
  the current frame texture **every display frame** from `gl_render_frame`
  (`ui/xemu.c:846` → `ui/xemu-thumbnail.cc`) so a save can bake a thumbnail.
- Menubar Machine→Snapshot submenu mirrors the slots (Card 3).

**Invariants we rely on.**
1. UI-initiated save/load runs on the display thread under BQL (Card 1)
   [DERIVED].
2. The disc-check guard exists **only** in this UI path. The operational path —
   `loadvm` over the monitor socket (Card 5) — bypasses it entirely
   [DERIVED; the monitor path is the one all rig runbooks use].
3. Slot names live in config; an empty string means unbound (`menubar.cc:122`).

**Danger notes.** Snapshot-as-benchmark methodology and its traps (near-death
save contamination, dead-screen-60) live in `rig-perf-measurement.md` — the UI
will happily load an unrepresentative save. Cross-build/hotplug compatibility
scars (CLI `-loadvm` vs hotplugged SBC) are B8's subject; nothing in this UI
defends against them.

**Escalation trigger.** Any change to load/save flow, slot semantics, or the
disc guard → upgrade alongside B8.

---

### Card 5 — Monitor console (`~`) + external monitor socket

**Purpose.** In-window QEMU HMP console, and the wiring that also lets us drive
the same HMP command set from outside the process (the rig workflow's backbone).

**Interfaces.**
- `MonitorWindow` (`monitor.cc`): toggled by `~` (or Debug→Monitor); bottom-half
  window; read-only scrollback + command line with up/down history;
  `ExecCommand` → `xemu_run_monitor_command()`.
- `ui/xemu-monitor.c`: a custom chardev (`TYPE_CHARDEV_XEMU_MONITOR`) passed to
  `monitor_init_hmp()` — created in `xemu_monitor_init()` from `xemu_hud_init`
  (`main.cc:135`). Monitor output accumulates in a static **48 KiB buffer**
  (`12*4096`, `:36`); `xemu_get_monitor_buffer()` exposes it to the window.
- External monitor: launching with
  `-monitor unix:/tmp/xemu-mon.sock,server,nowait` creates a **second,
  independent HMP monitor** via stock QEMU — nothing xui-specific. This is the
  operational control channel: `loadvm`/`screendump`/etc. via
  `nc -U /tmp/xemu-mon.sock` [VERIFIED — `runbooks/save-state-testing.md:62`,
  `graphical-dumps.md`, `rig-perf-measurement.md:70`].

**Invariants we rely on.**
1. Both monitors coexist as separate HMP sessions; the in-window buffer sees
   only the in-window session's output [DERIVED].
2. `~` toggling lives in `ProcessKeyboardShortcuts` and is not SBC-gated —
   available even in cockpit mode [DERIVED, `menubar.cc:71`].
3. `~` is the console, **not** the FPS display [VERIFIED — runbook].

**Danger notes.** In-window commands execute synchronously on the display thread
with BQL held — a slow HMP command stalls both UI and emulation [DERIVED]. The
scrollback buffer wraps by **resetting to offset 0** when full
(`xemu-monitor.c:75-80`): after a long output (`info qtree`), older text is
overwritten from the start — do not trust the visible transcript as a complete
log. Command echo is prepended as `# cmd`.

**Escalation trigger.** Changing the chardev, buffer policy, or threading of
command dispatch → Tier-B first (the external socket workflow depends on HMP
behavior staying stock).

---

### Card 6 — Input-manager + rebind UI

**Purpose.** Feeds controller state into ImGui navigation, decides when input
belongs to the UI vs the guest, and hosts the controller/keyboard rebinding
flow. (Device-level semantics — xid, scancode maps, SBC — are
`docs/internal/B6-input-stack.md`; this card is only the UI face.)

**Interfaces.**
- `InputManager g_input_mgr` (`input-manager.cc`): per-frame merge of **all**
  connected SDL gamepads (`available_controllers` from `ui/xemu-input.c`) into
  ImGui gamepad-nav keys (any button; axes above ~10%); tracks
  `MouseMoved()` (menubar wake, Card 3) and controller-vs-mouse navigation.
- Guest-vs-UI arbitration: when ImGui nav is active,
  `xemu_input_set_test_mode(1)` withholds controller input from the guest
  (`input-manager.cc:59` — "test mode" is a legacy name). Keyboard/mouse are
  arbitrated separately via `io.WantCaptureKeyboard/Mouse`
  (`xemu_hud_should_capture_kbd_mouse`, checked in `poll_events`).
- Event order in `poll_events` (`ui/xemu.c:898-905`): HUD first —
  `xemu_hud_process_sdl_events` gives a latent **rebind** first claim on the
  event (`g_main_menu.ConsumeRebindEvent`, `main.cc:174`) — then
  `xemu_input_process_sdl_events`, then the SDL2-lineage window/mouse handlers.
  In-code rationale: a controller detach must be able to cancel a rebind before
  the controller state is freed.
- Rebind UI (`main-menu.cc` InputView): four port buttons with live-rendered
  controller images (Duke / S / **Steel Battalion**, drawn by Card 9's
  `gl-helpers` into `controller_fbo`); a name↔binding table per device;
  clicking a binding creates `ControllerKeyboardRebindingMap` or
  `ControllerGamepadRebindingMap` (`ui/xemu-controllers.{h,cc}`) which consumes
  the next suitable SDL event. `Hide()` cancels any pending rebind.
- Per-frame guest input tick: `xemu_input_update_controllers()` in
  `poll_events` under BQL, immediately followed by `vr_sbc_panel_publish()` so
  the in-headset cockpit panel sees exactly that frame's state
  (`ui/xemu.c:981-989`).

**Invariants we rely on.**
1. While rebinding, events are swallowed before ImGui sees them; rebind state is
   owned solely by the main-menu input view [DERIVED].
2. Controller nav merges **all** pads — any pad can drive menus [DERIVED].
3. UI navigation never leaks to the guest (test mode) and vice versa: wake
   combos (GUIDE, BACK+START) only fire when no window has focus and no scene is
   up (`main.cc:274-296`) [DERIVED].
4. SBC mouse-aim normalizes against `viewport_coords[4]` — the letterboxed
   rectangle computed by `RenderFramebuffer` (Card 9), not the raw window
   (`ui/xemu-input.c:734-739`) [DERIVED].

**Danger notes.** The keyboard-SBC scancode map is read raw
(`SDL_GetKeyboardState`) and binds no modifier chords — modifier-chorded UI keys
also press their base key into the cockpit (documented for Ctrl+F8,
`ui/xemu.c:910-922`). The scancode-18 collision scar lives in B6. "Test mode" is
a misleading name for guest-input suppression — rename only with a Tier-B spec.

**Escalation trigger.** Any change to event ordering, capture arbitration, or
rebind lifecycle → Tier-B (jointly with B6).

---

### Card 7 — SBC cockpit overlay (pointer card)

**Purpose.** Desktop mirror of the Steel Battalion cockpit input state (gear,
pedals, toggles, lamps, aim meters) so keyboard pilots can see what the cockpit
thinks without the in-headset panel. **This is fork-original Tier-A code, not
Tier C** — it has a full spec in progress: → `docs/internal/A4-sbc-overlay.md`
*(being written in parallel — do not duplicate here)*, plus
`docs/features/sbc-cockpit-overlay.md`.

**Interfaces (one-line map only).** `ui/xui/sbc-overlay.cc`:
`SbcOverlayAvailable()` (true iff the keyboard drives an SBC),
`SbcOverlayProcessHotkey()` (default F3, `display.ui.sbc_overlay.toggle_scancode`;
collision guard disables the hotkey with a one-shot notification if the key is
claimed by a cockpit binding — cockpit wins), `SbcOverlayDraw()` (called every
HUD frame from `main.cc:329`). Config: `display.ui.sbc_overlay.{enable, corner,
opacity, show_key_labels, show_all_controls, toggle_scancode}`. View-menu item
mirrors the toggle (Card 3).

**Invariants we rely on (headline only — see A4).** Off-state draws nothing,
ever. The window is click-through/focus-free (`NoInputs`) because the mouse is
the aiming lever. The gear decode (254=R, 255=N, 1–5) is **field-verified; do
not re-derive** (ISS-B11) [VERIFIED — in-code note `sbc-overlay.cc:131-134`].
Key labels read the same generated-config scancode fields the input stack uses,
so a rebound layout self-describes.

**Danger notes / Escalation trigger.** All changes go through A4 — this card
exists only so the UI map has no hole. If A4 has not landed and a change is
urgent, write A4 first.

---

### Card 8 — Notifications, reporting, update plumbing (external endpoints)

**Purpose.** Toast/error surfaces, and **everything in the UI that talks to the
network**. Privacy-relevant: this is the complete phone-home inventory for the
UI layer.

**Interfaces.**
- Notifications (`notifications.cc`): C API `xemu_queue_notification/
  xemu_queue_error_message` (used across the whole codebase, incl. device code).
  One toast at a time, 4 s (`kNotificationDuration`, `notifications.hh:31`),
  top-right under the menubar; errors are a modal OK queue. With
  `display.ui.show_notifications` off, toasts are dequeued but not drawn
  (consumed silently) [DERIVED].
- **Endpoint 1 — compatibility reports** (`reporting.cc:32`):
  `https://reports.xemu.app/compatibility`, HTTP POST JSON. **Trigger: only the
  "Send" button** in Help→Report Compatibility (`compat.cc:208`). Nothing sends
  automatically. Payload (verified against `GetSerializedReport`,
  `reporting.cc:44-60`): user token (`general.user_token`), xemu
  version/commit/date, OS platform+version, CPU model string, GL
  vendor/renderer/version/GLSL strings, playability rating + free-text comments,
  and **the full XBE headers of the running title, base64-encoded**. The window
  shows the exact serialized report ("Report Details") and a privacy disclosure
  (server also records source IP). "Get Token" opens
  `https://reports.xemu.app` in a browser.
- **Endpoint 2 — update check** (`update.cc:32`):
  `https://api.github.com/repos/xemu-project/xemu/releases/latest`, HTTP GET.
  **Windows builds only** (`ui/xui/meson.build`: `update.cc` compiled only for
  `host_os == 'windows'`; all call sites `#if defined(_WIN32)`). Triggers:
  (a) **automatic at startup** when `general.updates.check` is true and the
  welcome window is not shown (`main.cc:153-157`) — toggle lives in
  Settings→General ("Check for updates whenever xemu is opened"); (b) manual via
  Help→Check for Updates. If an update exists the window opens itself. The
  update download fetches the release zip from GitHub
  (`browser_download_url`) and extracts over the install dir, renaming the
  running exe.
- **Browser opens** (user-click only, `SDL_OpenURL`): help docs
  (`menubar.cc:266`), `https://xemu.app` (welcome), token page (compat window),
  release-notes page (update window). `widgets.cc Hyperlink` is the generic
  mechanism.

**Invariants we rely on.**
1. **On Linux builds, the UI layer generates zero automatic network traffic**;
   the only outbound call is the manual compat-report POST [DERIVED — update.cc
   not compiled; nothing else in `ui/` uses `qemu/http.h`]. (Guest networking /
   nvnet is a separate subsystem, out of scope here.)
2. Compat reporting is opt-in per submission, with the payload inspectable
   before sending [DERIVED].

**Danger notes.** The update endpoint targets **upstream xemu-project releases**
— meaningless (and wrong) for PenguinBox binaries; any future Windows packaging
of the fork must repoint or strip it, and enabling any auto-update for
PenguinBox escalates this card immediately. Version comparison is a bare string
inequality (`update.cc:203`) — a local build newer than the latest release still
reads "update available". The compat report leaks host hardware details and
title identity by design — fine for upstream's public database, but a fork build
sending fork version strings upstream would be noise for them and disclosure for
us [ASSUMED risk — the Send path is manual, and Linux rigs don't build it].

**Escalation trigger.** Adding any endpoint, any automatic trigger, or wiring
update/reporting into PenguinBox release builds → Tier-B spec **plus** an
explicit privacy review note in the same change.

---

### Card 9 — Utilities: widgets, fonts, gl-helpers, viewport-manager

**Purpose.** The shared substrate every other card draws with: styled widget kit,
font atlas management, custom GL rendering (controller art, logo, and the
guest-frame blit itself), and DPI/scale management.

**Interfaces.**
- `widgets.cc`: xemu-styled `Toggle`, `Slider`, `FilePicker`, `ChevronCombo`,
  `SectionTitle`, `Hyperlink`, `HelpMarker`, `Logo` — all settings views are
  built from these. `misc.cc`: SDL3 native file/folder dialogs; **dialog
  callbacks are marshalled to the main thread via `SDL_RunOnMainThread` and run
  under `xemu_main_loop_lock`** (`misc.cc:26-40`), so pickers may safely touch
  QEMU state. Also contains SDL3 default-location workarounds.
- `font-manager.cc` (`g_font_mgr`): Roboto Medium (default), Roboto Condensed
  (menu sizes), FontAwesome subset, ABXY controller-glyph font, fixed-width;
  full atlas rebuild + GL font-texture recreate whenever viewport scale or
  pixel density changes (`Update()`).
- `viewport-manager.cc` (`g_viewport_mgr`): `m_scale` from SDL window display
  scale / pixel density (auto) or forced `display.ui.scale`; UI margin extents
  (25 px scaled, zeroed under 640 px width). Everything sizes via
  `g_viewport_mgr.Scale()`.
- `gl-helpers.cc` (1390 lines, the substrate): `Fbo` (offscreen render targets
  with save/restore of the previous binding); decal shaders (Mask / Logo /
  BlitGamma); `RenderDukeController` / `RenderControllerS` /
  `RenderSteelBattalionController` (live meters, gear display) +
  `RenderControllerPort`/`RenderXmu` into shared FBOs for the input view;
  animated SDF `RenderLogo`; **`RenderFramebuffer`** (`:1268`) — the actual
  guest-frame draw: filtering per `display.filtering`, fit/stretch/center math,
  aspect from `display.ui.aspect_ratio` with widescreen auto-detect
  (`xemu_get_widescreen`), DAC palette upload (256 uniforms/frame) for 8-bit
  modes, blanks when `nv2a_get_screen_off()`; writes the letterbox rectangle to
  the exported `viewport_coords[4]`. `RenderFramebufferToPng`/`SaveScreenshot`
  (F12 → fpng PNG into `general.screenshot_dir` or cwd, timestamped name).
  `InitCustomRendering` (`:468`) loads all textures/shaders at HUD init.

**Invariants we rely on.**
1. All utility GL work runs on the display thread's window GL context; FBO
   helpers restore previous state [DERIVED].
2. `viewport_coords` is a **cross-component contract**: `RenderFramebuffer`'s
   letterbox output feeds SBC mouse-aim normalization
   (`ui/xemu-input.c:734-739`). Changing the fit math changes aim mapping
   [DERIVED].
3. Screenshots capture the guest texture (pre-HUD), not the window — HUD/overlay
   never contaminate F12 captures [DERIVED, `SaveScreenshot(g_tex)` path].
4. Font rebuilds are safe mid-run because they happen at frame start
   (`g_font_mgr.Update()` in `xemu_hud_update` before NewFrame) [DERIVED].

**Danger notes.** `RenderFramebuffer` re-queries texture dimensions and re-uploads
the 256-entry palette every frame — cheap but on the hot present path; any
"optimization" here must respect the DAC palette's per-frame mutability
[ASSUMED]. The SBC controller art in `gl-helpers.cc` carries the field-verified
gear decode twin of Card 7's — keep them in lockstep (ISS-B11).

**Escalation trigger.** Changing `RenderFramebuffer` (scaling/aspect/palette/
screen-off), `viewport_coords` semantics, or the screenshot path → Tier-B first;
widget/font/DPI-only tweaks may stay Tier C with a card update.

---

*End of C2. Nine cards; biggest open unknowns are flagged inline as [ASSUMED]:
the exact GL context MangoHud hooks (Card 1), scene-wedge behavior (Card 2), and
the per-frame palette-upload mutability assumption (Card 9).*
