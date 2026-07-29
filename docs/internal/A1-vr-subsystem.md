# Spec — VR subsystem

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| A (fork-original; full behavioral spec) | `hw/xbox/nv2a/pgraph/vk/vr_manager.c`, `vr_xr_loader.{c,h}`, `vr_xr_session.c`, `vr_xr_compositor.c`, `vr_camera.c`, `vr_sbc_panel.{c,h}`, `vr.h`, `vr_public.h`, `vr_internal.h`; config `config_spec.yml:374-465`; hooks in `vk/instance.c`, `vk/renderer.c`, `vk/display.c`, `ui/xemu.c`, `ui/xui/main-menu.cc` | v1 draft | 2026-07-27 |

Confidence tags: **[VERIFIED]** = citable evidence (field run, log, review finding); **[DERIVED]** =
read from code and traced, never deliberately exercised; **[ASSUMED]** = believed, unproven.

## Purpose & scope

The VR subsystem ("PenguinBox"; code strings still say `xemu-vr`) is a **sidecar OpenXR layer over
xemu's Vulkan NV2A renderer**. It presents the finished guest frame as a world-locked, head-tracked
screen in the headset (Tier 1), optionally drives a per-game head-look camera by poking guest RAM
(Tier 3, `vr_camera.c`), and optionally composites a Steel Battalion cockpit instrument panel as a
second world-locked quad (`vr_sbc_panel.c`). Tier-2 per-eye stereo is a non-goal today
(`AGENTS.md:19-23`; dormant `chains[1]`, `vr_internal.h:87-89`).

**Originality claim (hard rule H-7, `AGENTS.md:43`): this fork did NOT invent the core
techniques.** World-locked screen + runtime reprojection is standard OpenXR (Bigscreen/Virtual
Desktop class); head-look camera injection is the flat2VR/UEVR lineage. The fork's claim is
**execution + the per-game grind** (RAM-hunt loop, gates, durable anchoring, the SBC panel) on
xemu's NV2A/Vulkan path. This spec documents behavior, not invention.

Design lineage: a C port of the team-authored PCSX2-VR Vulkan module (`docs/vr/01-port-map.md` §0
fragment-audit clean; §6 function map). Plan of record + field history: `docs/vr/00-mvp-plan.md`
(Tier-1 MVP signed off in-headset 2026-07-16, plan §8); stereo-era lessons feeding the comfort
policies: `docs/vr/xbox-vr-3d-pipeline-field-guide.md`. This spec consolidates and cites those
docs, not duplicates them.

## External interface

**Config keys** (`config_spec.yml`; code-generated into `g_config.vr.*` at build time — a settings
change is a spec edit + rebuild, `AGENTS.md:162-166`):

- `vr.enable` (bool, default false, `config_spec.yml:375-377`) — master runtime gate; see I1.
- Screen: `screen_distance` (2.0 m), `screen_height` (1.4 m), `screen_arc` (0.0 deg; cylinder when
  >= 5 and the runtime supports it), `screen_vertical_offset` (0.0 m) (`config_spec.yml:378-389`).
  Read every frame directly from `g_config` — live-tunable from the HUD sliders
  (`ui/xui/main-menu.cc:793-811`).
- Tier-3 head-look: `headlook_enable`, `headlook_sensitivity` (1.0), `headlook_invert_pitch`,
  `headlook_engine_period` (0 = radians), `headlook_yaw_addr`/`_pitch_addr`/`_gate_addr` (hex
  strings), `headlook_gate_value` (1) / `_gate_mask` (255), and the durable-anchor block
  `headlook_base_mode` ('' | pointer | aob), `_base_ptr`, `_base_offset`, `_base_aob`,
  `_base_scan_start/_end`, `_base_validate_offset/_equals` (`config_spec.yml:390-440`).
- `vr.hunt_enable` (default false, `config_spec.yml:441-443`) — arms the F9 full-RAM dump.
  **Deliberately independent of `vr.enable`** (see I2).
- SBC panel: `sbc_panel_enable` (false), `sbc_panel_anchor` (enum console|screen|world, default
  console), `sbc_panel_distance` (0.75 m), `sbc_panel_drop` (0.40 m), `sbc_panel_tilt_deg` (35),
  `sbc_panel_width_m` (0.50) (`config_spec.yml:447-465`).
- `display.renderer` (enum NULL|OPENGL|VULKAN, **default OPENGL**, `config_spec.yml:467-471`) — VR
  requires VULKAN; see I3/P1.

**Hotkeys** (UI thread, `ui/xemu.c:906-941`): F8 = recenter, F9 = hunt dump — both stand down when
the keyboard feeds an SBC controller (cockpit toggles win, ISS-B12); Ctrl+F8 = recenter that always
fires (escape hatch; under the stock SBC layout the chord also flips the filter toggle once,
`ui/xemu.c:910-926`). Menu item: `ui/xui/menubar.cc:234`.

**Environment flags read by the binary:** `XEMU_VR_PANEL_DUMP=<path>.ppm` — on every SBC-panel
content change, rasters the panel on the UI thread into a scratch buffer and writes a PPM
alpha-composited over the void color (16,18,26). It dumps the panel's exact 768x256 pixels —
placement is XR-only, but CONTENT is fully verifiable with no headset (`vr_sbc_panel.c:460-483`).
Launcher-level env (not read by the binary): `XEMU_VR=1` (toml `[vr]` enable management),
`XEMU_VR_RUNTIME=system`, `XEMU_TOML` (`tools/vr/rig-boot-test.sh:7-8,26,32-44`).

**OpenXR runtime requirements:** system loader `libopenxr_loader.so.1` (fallback `.so`) found via
dlopen — no link-time dependency; VR-off users carry no libopenxr requirement
(`vr_xr_loader.c:58-76`; port-map delta D1). The runtime must offer `XR_KHR_vulkan_enable2`
(required) and `VK_FORMAT_R8G8B8A8_SRGB` swapchains; `XR_KHR_composition_layer_cylinder` is probed
and enabled opportunistically (`vr_xr_session.c:111-144`, `vr_xr_compositor.c:985-1011`). API
version pinned to OpenXR **1.0** — never the 1.1 headers' `XR_CURRENT_API_VERSION` (rig loader is
1.0.20; `vr_xr_session.c:146-157`). Rig reality (H-6, `AGENTS.md:42`): `XR_RUNTIME_JSON` must
point at the WiVRn manifest (system default Monado has no HMD there) and
`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` is MANDATORY with VR on; the
launcher enforces both (`rig-boot-test.sh:21,50-63`).

**Renderer-facing API** (upstream seams; sequence contract in `vr.h:16-37`):

| Call | Seam |
|---|---|
| `xemu_vr_begin_bootstrap` (Phase A) | top of `pgraph_vk_init_instance`, `instance.c:634` |
| `xemu_vr_create_instance` | replaces `vkCreateInstance`, `instance.c:233` |
| `xemu_vr_get_physical_device` | `select_physical_device`, `instance.c:380` |
| `xemu_vr_create_device` | replaces `vkCreateDevice`, `instance.c:570` |
| `xemu_vr_on_device_created` (Phase B) | after `vkGetDeviceQueue`, `instance.c:642` |
| `xemu_vr_frame` | `pgraph_vk_sync`, right after `pgraph_vk_render_display`, `vk/renderer.c:142` |
| `xemu_vr_on_device_destroyed` | head of `pgraph_vk_finalize_instance`, `instance.c:662` |

UI-safe surface (no target headers): `xemu_vr_request_recenter`, `xemu_vr_request_hunt_dump`,
`xemu_vr_active` (`vr_public.h:24-32`); `vr_sbc_panel_publish` once per poll after
`xemu_input_update_controllers` (`ui/xemu.c:981-989`); and the WIP interaction boundary
`vr_sbc_panel_cells` / `vr_sbc_panel_inject` (log-only stub, `vr_sbc_panel.c:729-761`).

## State & data structures

One process-global `XemuVRState g_xemu_vr` (`vr_internal.h:204-216`):

- **`VRXrSession`** (`vr_internal.h:30-41`): XrInstance/systemId/session, LOCAL reference space,
  session-state machine (zero → InstanceReady → SessionCreated → Running; STOPPING ends the
  session and may re-begin on READY; LOSS_PENDING/EXITING/instance-loss set `lost`),
  `session_running` (qatomic — the only field the pacer thread reads), `cylinder_supported`, and
  the runtime's supported Vulkan version range (feeds the apiVersion clamp).
- **`VRCompositor`** (`vr_internal.h:49-160`): captured VkDevice/queue/family so stop() is
  self-sufficient; pacer thread + one-slot mailbox (`pending_frame_state`/`has_pending_frame`
  under `vr_frame_mutex`); `begin_owed` debt; two `VREyeChain`s (mono uses `chains[0]`,
  `eyeVisibility=BOTH`; `chains[1]` stereo-reserved dormant), each with the acquire/wait edge
  machinery (`ever_released`, `wait_pending`/`pending_index`); the per-row Y-flip `VkImageCopy`
  table; VIEW space + stashed head pose; recenter flag + `recenter_pose` (identity until first
  recenter); a 2-deep command-buffer/fence ring; the SBC panel block (dedicated `panel_chain`,
  persistently-mapped staging buffer, dedicated cmd + fence, `panel_dirty`); warning latches.
- **`VRCamera`** (`vr_internal.h:166-202`): cached parses of the `headlook_*` strings;
  dynamic-base state (pointer/AOB modes, cached AOB match + rescan throttle); ADS-gate edge state;
  freelook zeroing state (`yaw_base`, `head_ref_*`); compose-mode last-written values (fold stick
  aim into the base); `hunt_requested` (qatomic) + dump counter.

Threading contract (`vr.h:10-14`, `vr_internal.h:26-29`, port-map §3): everything runs on the
**pfifo thread** (the thread owning every Vulkan call in the VK renderer) except (a) the pacer,
whose sole OpenXR call is `xrWaitFrame`; (b) UI-thread qatomic request flags and the seqlocked SBC
snapshot writer (`vr_sbc_panel.c:374-388`); (c) detached hunt-file writer threads
(`vr_camera.c:326-373`). The SBC crossing is a `QemuSeqLock` single-writer slot whose sequence
doubles as the content generation for change-gating (`vr_sbc_panel.c:446-458,623-646`).

Build shape: the `vr_*` sources compile unconditionally with the Vulkan backend
(`vk/meson.build:1,22-27` — inside `if vulkan.found()`); there is **no compile-time VR option** —
gating is runtime-only (`AGENTS.md:167-175`).

## Invariants

- **I1 — Off-state identity.** With `vr.enable = false` (and the other `vr.*` keys at defaults)
  behavior MUST be identical to upstream xemu (stated hard requirement, `CLAUDE.md` build/test
  section; `AGENTS.md:167-171`). Mechanics: the single active predicate `xemu_vr_wanted() =
  g_config.vr.enable && renderer == VULKAN` (`vr_manager.c:27-31`) makes `xemu_vr_begin_bootstrap`
  return before any side effect — no dlopen, no XR call (`vr_manager.c:46-48`); with
  `bootstrap_active` false every Vulkan seam passes through verbatim to `vkCreateInstance` /
  xemu's own device selection / `vkCreateDevice` (`vr_manager.c:70-80,85-87,102-112`); the frame
  hook reduces to a `vr.hunt_enable` check plus a `session_running` check
  (`vr_manager.c:166-175`, `vr_camera.c:401-403`); panel publish is two bool reads
  (`vr_sbc_panel.c:412-414`); teardown is an idempotent no-op (`vr_manager.c:140-145`). Residue
  when off: a few always-false branches per frame, F8/F9 handlers setting never-consumed atomic
  flags (`ui/xemu.c:923-941`), and the HUD section (`ui/xui/main-menu.cc:787-812`). **[DERIVED]**
  — traced end-to-end in code, but **no automated off-state A/B gate exists**: plan W5.2 proposed
  one (`00-mvp-plan.md:262-264`) with no record it ran. "Identical" = behaviorally identical, not
  binary-identical (the code is always compiled in).
- **I2 — Hunt is VR-independent by design.** F9 full-RAM dumps fire in flat mode too, gated only
  on `vr.hunt_enable` and polled BEFORE the session gate (`vr_manager.c:167-171`; rationale +
  optional head-pose metadata `vr_camera.c:388-413`). This is the one deliberate
  `vr.enable`-independent behavior: with `hunt_enable=true` a VR-off run is NOT
  upstream-identical (F9 writes `~/xemu-vr-hunt/dump-NNN.{bin,txt}`). **[VERIFIED]** — the OFP
  18-dump capture was taken exactly this way (`00-mvp-plan.md` §9b).
- **I3 — VR requires the VULKAN NV2A path (H-3, `AGENTS.md:39`).** `xemu_vr_wanted()` refuses on
  any other renderer (`vr_manager.c:29-30`), so a GL/NULL run never touches OpenXR. The former
  hazard, fixed 2026-07-28: `get_default_renderer()` prefers OPENGL (`pgraph.c:277-291`),
  `display.renderer` defaults to OPENGL (`config_spec.yml:471`), and a failed VK init falls back
  to the default renderer — this was previously **silent** (a fading notification only) and
  produced the "VR on, renderer GL, healthy log, flat-only output" trap. `init_renderer` now
  prints a framed, unmissable stderr banner, appends an explicit *"VR requires Vulkan and IS
  INACTIVE this session"* line whenever `vr.enable` was true and the failed renderer was Vulkan,
  and drives a persistent (non-fading) red warning in both the Display settings tab and the Video
  Debug overlay via `nv2a_get_renderer_fallback_active/reason()` (`nv2a-renderer-status.h`).
  Enforcement is still primarily operational (`renderer = 'VULKAN'` pinned in the deployed tomls:
  `tools/vr/flat-check.toml:23`, `sb-vr.toml:36`, `sb-verify.toml:34`), but a violation is no
  longer silent if it happens — the launcher/log-expectation layer (plan §8 step 3) is now a
  second line of defense, not the only one. Gate mechanics **[VERIFIED]** (field incident class,
  plan §1/§7 F3); the loud-fallback fix **[VERIFIED — win64-cross build clean, not yet exercised
  live]** (see Confidence & unknowns for what live exercise would still confirm).
- **I4 — A VR failure can never fail renderer init or crash the emulator.** Every seam failure
  calls `vr_abort_bootstrap()` and falls back to the plain Vulkan call (`vr_manager.c:67-113`;
  contract `vr.h:13-14`, `vr_manager.c:14-17`); every entry point is safe when VR is disabled or
  bootstrap failed. `vr_abort_bootstrap` is idempotent, ordered compositor → camera → session →
  instance (`vr_manager.c:34-42`; port-map D7). **[DERIVED]** (legs traced, not all injected).
- **I5 — The pfifo thread never blocks on XR pacing.** Only the pacer calls `xrWaitFrame`; the
  frame hook consumes a one-slot mailbox and submits nothing when it is empty
  (`vr_xr_compositor.c:857-896,1232-1240`; port-map §3: no condition variables — xrWaitFrame's
  "blocks until the previous frame is begun" IS the handshake). Bounded blocking the pfifo thread
  DOES accept: <= 1 s fence waits and <= 1 s `xrWaitSwapchainImage`
  (`vr_xr_compositor.c:265-278,394-421`; cost analysis plan §7 F4). **[VERIFIED]** — ran
  end-to-end in the first in-headset session (plan §8 + sign-off).
- **I6 — Begin/end pairing debt (`begin_owed`).** If `xrBeginFrame` fails after a mailbox frame
  was consumed, the debt is recorded and settled with begin + zero-layer end on later visits —
  **including visits where the mailbox is empty** (the F1 wedge fix: settle BEFORE the mailbox
  check, `vr_xr_compositor.c:1221-1228,900-927`; plan §7 F1). `XR_ERROR_SESSION_NOT_RUNNING`
  clears the debt. **[DERIVED]** (fix reviewed; path never deliberately exercised).
- **I7 — Swapchain acquire/wait/release discipline.** On wait timeout/failure the image STAYS
  acquired and the next visit RE-WAITS the same image — never re-acquires (re-acquiring leaks one
  acquire per timeout until the chain wedges with `CALL_ORDER_INVALID`); after a successful wait
  the image MUST be released even if the copy submission failed (else the acquire leaks);
  `ever_released` gates layer submission; a timed-out visit submits zero layers
  (`vr_xr_compositor.c:366-443`; same discipline on the panel chain, `:786-846`; flags
  `vr_internal.h:55-62`). **[DERIVED]** (port-map §4 "port these exactly"; timeouts not exercised).
- **I8 — Copy contract and source-layout restoration (port-map D3).** Source is
  `r->display.image` — or, when this sync's composite went the async-present path, the CURRENT
  present slot's published image (`vr_xr_compositor.c:1307-1319`) — left in
  `SHADER_READ_ONLY_OPTIMAL` by `render_display`. The copy barriers it to `TRANSFER_SRC` **and
  back**: xemu has no layout tracker and the GL mirror samples that image right after
  `sync_complete` (`vr_xr_compositor.c:20-31,293-341`). Same queue (`r->queue`) + same pfifo
  thread as `render_display`'s submit — submission order makes the finished frame visible with no
  extra semaphores (`vk/renderer.c:138-146`). Display images carry `TRANSFER_SRC` usage for this
  (`vk/display.c:754-757,912-916`). **[VERIFIED]** — desktop mirror confirmed unaffected in the
  signed-off in-headset run (plan §8).
- **I9 — Y-flip + gamma rule.** xemu's display image is GL-heritage memory (row 0 = picture
  BOTTOM); the copy flips vertically with one single-row `VkImageCopy` per line, preserving raw
  bytes — UNORM source bytes land in the `R8G8B8A8_SRGB` swapchain unre-encoded ("gamma-encoded
  bytes, encoded once"; a `vkCmdBlitImage` would re-encode gamma and wash the image out)
  (`vr_xr_compositor.c:234-250,316-320`, `vr_internal.h:93-100`). **[VERIFIED]** — field finding
  F8: the first in-headset image was upside down; the fix was confirmed in-headset
  (`00-mvp-plan.md:494-506`).
- **I10 — Teardown order.** Signal pacer exit → drain owed begins/mailbox with begin+zero-end
  (bounded ~2 s poll) → join pacer → destroy VIEW space → wait all fences → destroy screen chains
  → destroy panel (its own fence waited first) → destroy fences/pool — all while the VkDevice is
  alive, at the head of `pgraph_vk_finalize_instance` before `vkDestroyDevice`
  (`vr_xr_compositor.c:1135-1210`, `instance.c:656-662`); then session, then instance
  (`vr_manager.c:34-42`). Session destroy precondition: pacer already joined — nobody blocked in
  `xrWaitFrame` (`vr_xr_session.c:410-414`). Runtime renderer switches funnel through the same
  finalize/init ops (plan W2). **[DERIVED]** (review F6 clean; switch leg never exercised).
- **I11 — Panel off-state and isolation.** With `sbc_panel_enable=false` the compositor does zero
  panel work (`vr_xr_compositor.c:1346`, `config_spec.yml:444-446`); enabled-but-inactive (no
  keyboard-fed SBC controller) publishes an inactive snapshot and composites nothing
  (`vr_sbc_panel.c:32-38,420-424,633-637`). Every panel failure only hides the panel — it never
  fails the frame or VR (`vr_xr_compositor.c:445-453,1339-1344`). Change-gating: raster only when
  the seqlock generation moved; upload only when dirty or never-released; `panel_dirty` persists
  across failed uploads so new content can never strand behind a stale image
  (`vr_sbc_panel.c:639-645`, `vr_internal.h:139-143`, `vr_xr_compositor.c:1368-1389`); staging
  starts fully transparent — unwritten bytes read invisible, never garbage
  (`vr_xr_compositor.c:574-578`). **[DERIVED]**; raster content **[VERIFIED]** headset-free via
  `XEMU_VR_PANEL_DUMP` (`docs/session-2026-07-23_25-log.md:67`).
- **I12 — Head-look write conditions.** `vr_camera_frame` pokes guest RAM only when ALL hold:
  live session frame, valid head pose, `headlook_enable`, valid yaw address (pitch optional),
  gate ON if configured (`(byte & mask) == value`; the gate address is ALWAYS absolute, never
  rebased), and base resolved in the configured mode. Any miss sidelines the poke and forces a
  fresh zero on return — never a snap, never a write into dead memory (`vr_camera.c:415-483`).
  Zeroing captures the game's current angles + the current head angles at enable / gate rising
  edge / F8 (`vr_camera.c:485-502`); compose mode folds the game's own writes into the base so
  stick and head aim ADD instead of fighting (`vr_camera.c:504-528`); yaw seams wrap in engine
  units (`headlook_engine_period`, `wrap_period`, `vr_camera.c:46-56`). Mechanics **[VERIFIED]**
  in-headset on OFP (plan §9-§9b); cross-boot pointer-anchor durability **[ASSUMED]** (the §9b
  reboot test defines the pass; its outcome is not recorded in these docs).
- **I13 — OpenXR version/format pins.** apiVersion `XR_MAKE_VERSION(1,0,0)`
  (`vr_xr_session.c:151-153`) **[VERIFIED]** (rig probe PROBE_OK + live sessions, plan increments
  2/§8); `xrGetVulkanGraphicsRequirements2KHR` mandatory before session creation
  (`vr_xr_session.c:215-224`) **[DERIVED]**; requested VkInstance apiVersion clamped into the
  runtime's range via shallow copies, never mutating xemu's create-info (`vr_xr_session.c:262-288`)
  **[DERIVED]**; XR-mandated physical device overrides `preferred_physical_device`
  (`instance.c:377-393`) **[VERIFIED]** (log line observed live, plan §8); session binds
  `r->queue`'s family, queueIndex 0 (`vr_xr_session.c:365-377`; port-map D9) **[DERIVED]**.

## Policies & decision points

- **P1 — Renderer policy (H-3):** operators/launchers set `display.renderer = 'VULKAN'` in the
  toml; code-side, VR only refuses non-VK (I3). A half-initialized state is prevented by the
  gate; a *silent flat* state — fixed 2026-07-28 — is now also prevented in-code (I3): the
  operational layer (log expectations, launcher) is a second line of defense, not the only one.
  **[VERIFIED]** as the shipped policy and, as of this fix, backed by an in-code loud detector.
- **P2 — Runtime selection (H-6):** the launcher auto-selects the WiVRn manifest via
  `XR_RUNTIME_JSON` (default Monado = flat with a healthy log — plan §7 F3), pre-checks the
  compositor socket, forces NVIDIA PRIME, and manages the toml `[vr]` section because xemu's
  auto-save strips default-valued sections (`rig-boot-test.sh:11-15,31-63`; `AGENTS.md:163-166`).
  Never launch the bare binary for headset runs. **[VERIFIED]** (F3 field class).
- **P3 — Screen shape:** flat `XrCompositionLayerQuad` by default; cylinder section when
  `screen_arc >= 5` deg AND the runtime offers the extension (`vr_xr_compositor.c:1404-1461`).
  Anchor = `recenter_pose` (identity until first F8): yaw-only orientation + head position; quad
  center = anchor ⊕ yaw-rotated `(0, voffset, -distance)`, size `(height*aspect, height)`;
  cylinder pose is the viewer origin, radius = distance. Aspect = display-image ratio
  (anamorphic/widescreen refinement tracked as plan U7; port-map D8). layerFlags 0 = opaque
  screen. **[VERIFIED]** in-headset (Tier-1 sign-off).
- **P4 — Recenter semantics (F8):** UI sets a qatomic; the frame consumes it, flattening the
  current head pose to yaw + position (the screen never tilts or rolls) and re-zeroing head-look
  in the same act — "this is my new forward" for screen AND camera at once
  (`vr_xr_compositor.c:1287-1302`, `vr_camera.c:382-386`). Runtime-side LOCAL recenters are
  honored implicitly (event logged; the quad is positioned in LOCAL and follows,
  `vr_xr_session.c:472-476`). SBC keyboard mode: bare F8/F9 stand down; Ctrl+F8 recenters
  (ISS-B12 policy, `ui/xemu.c:909-941`). **[VERIFIED]** (W4 field use).
- **P5 — Frame policy:** consume at most one paced frame per sync; empty mailbox = submit nothing
  (the runtime re-projects the last layer); `shouldRender=false` or no display image (VGA-blit
  boot phases) = layers from the last released images or zero layers; a skipped screen copy keeps
  the last released image; a wait timeout forces a zero-layer visit
  (`vr_xr_compositor.c:1305-1341,1395-1404`). The panel layer is independent — a screen-side
  zero-layer visit can still present the panel alone (`vr_xr_compositor.c:1465-1469`).
  Stale-image-over-black-flash is the inherited comfort posture
  (`xbox-vr-3d-pipeline-field-guide.md:200-204`). **[DERIVED]**.
- **P6 — SBC panel placement:** anchor `console` (default) = low/near, tilted up
  `sbc_panel_tilt_deg` about X, anchored to the SAME recenter pose as the screen (F8 moves both);
  `screen` = a dash directly below the screen (screen yaw/distance, dropped, no tilt); `world` =
  raw LOCAL offset, F8 leaves it (`vr_xr_compositor.c:1483-1528`). Blended on straight source
  alpha (`UNPREMULTIPLIED`, `vr_xr_compositor.c:1530-1534`); quad size `width x width*H/W` of the
  fixed 768x256 raster (`vr_sbc_panel.h:37-38`). Content policy (gear decode 254=R/255=N per
  ISS-B11, toggle-lamp bit derivation, unipolar pedals / bipolar aim+rotation) mirrors the
  shipped flat overlay's field-verified semantics — do not re-derive
  (`vr_sbc_panel.c:40-49,253-273`). Placement **[DERIVED]** (XR-only, needs headset); content
  **[VERIFIED]** via panel dump.
- **P7 — Tier-3 base-mode decision:** absolute (found address IS the poke target) → pointer-deref
  (static pointer + offset, optional validation dword — the durable default when a static pointer
  exists) → AOB signature scan (cached match, cheap per-frame revalidate, ~1 s throttled rescan
  on loss) (`vr_camera.c:219-301`; decision tree `AGENTS.md:68-90`; OFP turn-key values
  `00-mvp-plan.md` §9b). Physical addresses end-to-end (`d->vram_ptr`; no VA translation — a hunt
  hit IS the poke address). Mechanics **[VERIFIED]** on the OFP captures; the AOB leg
  **[DERIVED]** (no field-confirmed title).
- **P8 — Operational hard rules that are part of this feature's contract:** H-2: live-session
  check before ANY rig VR/GPU action — a VR launch seizes the live headset; use
  `pgrep -f "[q]emu-system-i386"` (`-x` silently never matches the >15-char name; keep the `[q]`
  bracket over ssh); never chain a launch behind the check (`AGENTS.md:38`). **H-9: never stop
  `wivrn-server` while a headset client is connected — WiVRn 26.6.1 segfaults. Recovery order:
  quit the emulator → close the headset client → restart the server** (`AGENTS.md:45`). The
  in-code teardown discipline (fences waited before chain destruction, "H-9-correct",
  `vr_internal.h:121-131`, `vr_xr_compositor.c:1179-1187`) makes quitting the emulator first
  always safe. **[VERIFIED]** — field-proven crash class shared across all three cores.

## Failure modes

All degrade to flat rendering with an `xemu-vr:` stderr line; none may fail renderer init (I4).

- **No loader / Windows:** dlopen fails → "OpenXR loader not found … Running flat"
  (`vr_xr_loader.c:68-76`). On Windows the dlopen shim always fails cleanly — VR is a
  Linux-rig-only path (`vr_xr_loader.c:12-34`). [DERIVED]
- **No runtime, or one without vulkan_enable2:** `xrCreateInstance` fails → flat
  (`vr_xr_session.c:163-171`). **No headset:** `xrGetSystem` fails → "no HMD system available …
  Running flat" (`vr_xr_session.c:193-199`). [VERIFIED — headless probe, plan increment 2]
- **Wrong runtime bound (Monado):** everything "succeeds" flat — the most expensive false pass;
  prevented by P2, detected by the log expecting `OpenXR runtime: WiVRn` (plan §8). [VERIFIED — F3]
- **Seam failures (instance / physical device / device through XR):** abort VR, fall back to the
  plain Vulkan call (`vr_manager.c:67-113`). One deliberate exception: an XR-mandated GPU lacking
  xemu's required features is a renderer-init error (`instance.c:381-386`). [DERIVED]
- **Session/compositor start failures** (session, LOCAL space, missing `R8G8B8A8_SRGB`,
  pool/buffers/fences): abort to flat (`vr_manager.c:115-134`, `vr_xr_compositor.c:964-1058`).
  VIEW-space failure alone is non-fatal — head pose stays inert (`vr_xr_compositor.c:1099-1111`).
  [DERIVED]
- **Mid-session loss:** STOPPING ends the session (may re-begin on READY); LOSS_PENDING /
  EXITING / instance-loss / poll failure → `lost` → full teardown; the XR-created VkDevice stays
  alive and flat rendering continues (`vr_xr_session.c:60-103,434-487`, `vr_manager.c:181-187`).
  Covers WiVRn server death mid-play. [DERIVED; STOPPING form field-observed, plan §8 table]
- **Per-frame degradations:** swapchain create failure = frame skipped, retried next frame,
  warn-once (`vr_xr_compositor.c:180-190,219-231`); acquire failure = keep last image; wait
  timeout/failure = zero-layer visit + re-wait (I7); fence timeout = skip copy; `xrBeginFrame`
  failure = begin_owed (I6); `xrEndFrame` failure = warn-once, continue
  (`vr_xr_compositor.c:1560-1564`). Panel legs mirror these, only hiding the panel (I11). [DERIVED]
- **Boot phases:** no NV2A surface during VGA-blit boot → no XR frames; the headset holds the
  void/last image — a few seconds is normal (plan §8 step 4). [VERIFIED]
- **Environment (AGENTS.md §5 / plan U5):** the VULKAN renderer's display path hard-requires
  NVIDIA GL external-memory interop (`HAVE_EXTERNAL_MEMORY` hardcoded 1, `vk/renderer.h:44`) — it
  will **not** run under Xvfb/llvmpipe. Headless smoke tests use `renderer = NULL`/OPENGL (which
  per I3 also means no VR bootstrap); real VR validation needs the rig's live X + NVIDIA GL + the
  PRIME env (`AGENTS.md:158-161`, `rig-boot-test.sh:18-21`). Bare SSH cannot create XR sessions
  (S0 lesson, plan increment 2). [VERIFIED — field]

## Instrumentation

- **stderr `xemu-vr:` log stream** with one-shot warning latches per failure class, reset at each
  compositor start (`vr_internal.h:145-159`, `vr_xr_compositor.c:1084-1097`). Transition lines:
  bootstrap phases, runtime/HMD identity, Vulkan range, session-state changes
  (`vr_xr_session.c:64-66`), swapchain creation, recenter (yaw + eye height), head-look zero
  (game yaw, head ref, resolved base), AOB resolution, hunt-dump completion.
- **Phase timers:** `xemu_vr_frame` cost is measured as the present-bucket `VRSUBMIT` sub-phase
  (`hw/xbox/nv2a/pgraph/phase_timers.h:381-387`; hook `vk/renderer.c:141-146`) — the VR-submit
  half of present is separable in perf traces.
- **`XEMU_VR_PANEL_DUMP=<path>.ppm`** — headset-free ground truth for the SBC panel's pixels;
  written on every content change, UI thread, alpha-composited over the void color
  (`vr_sbc_panel.c:460-483`).
- **F9 hunt dumps** — `~/xemu-vr-hunt/dump-NNN.bin` + `.txt` metadata (head yaw/pitch in degrees,
  size); consumed by `tools/vr/hunt_scan.py` (self-tested, `tools/vr/test_hunt_scan.py`).
- **HUD:** `xemu_vr_active()` retitles the settings section "Virtual Reality — session active"
  (`ui/xui/main-menu.cc:787-788`; racy-by-contract, display only, `vr_manager.c:160-164`).
- **Log-only inject stub** for panel interaction (`vr_sbc_panel.c:757-761`) — exercises the
  hit-test cell table without input plumbing.

## Verified-by

- **Tier-1 in-headset sign-off, 2026-07-16** (`00-mvp-plan.md:509-515`): full lifecycle (WiVRn
  selection, Phase A/B, session, pacer, swapchain, copy, quad) ran end-to-end on the first live
  attempt; F8 orientation bug found, fix confirmed in-headset; desktop mirror unaffected.
  Primary evidence for I5, I8, I9, I13, P3.
- **Two adversarial review passes** (plan §7/§8): F1 owed-begin wedge fixed; D3 layout claims
  checked against `render_display`'s actual barriers; single-slot mailbox property argued from
  xrWaitFrame semantics; teardown ordering reviewed clean (F6); apiVersion clamp added.
- **Headless rig probe** (plan increment 2): dlopen + `XR_KHR_vulkan_enable2` + cylinder support
  confirmed against the rig loader (PROBE_OK).
- **Tier-3 field sessions 2026-07-17/19** (plan §9-§9b): hunt→scan→poke loop exercised on OFP:
  Elite; ADS gate + freelook zeroing exercised in-headset; the camera-relocation failure was
  discovered live and the pointer/AOB anchoring shipped in response.
- **Panel raster** verified headset-free via `XEMU_VR_PANEL_DUMP`
  (`docs/session-2026-07-23_25-log.md:67`); gear decode field-verified (ISS-B11, `KNOWN_ISSUES.md:385`).
- **Not verified by any automated gate:** off-state identity (I1), timeout/loss edges (I6, I7),
  renderer-switch rebuild (I10), panel placement (P6), cross-boot anchor durability (I12). There
  are no unit tests for the `vr_*` C sources.

## Confidence & unknowns

High confidence (field-proven): lifecycle, copy path, pacing, Y-flip/gamma, recenter, hunt loop,
WiVRn envelope. Medium (code-traced only): loss/timeout edges, begin_owed, teardown under partial
failure, panel upload edges. Open items, largest first:

1. **Off-state identity is asserted, not gated.** I1 is [DERIVED]; the contract says
   MUST-identical, and nothing today would catch a future `vr_*` change leaking side effects into
   a VR-off run (e.g. a hook added before a gate). The flat A/B harness proposed in plan W5.2
   remains unbuilt.
2. **H-3's "detect non-VK LOUDLY" — implemented 2026-07-28, not yet exercised live.**
   `xemu_vr_wanted()` still silently no-ops when `vr.enable=true` on a GL/NULL renderer (that
   part of the contract is by design — I4, a VR failure can never fail renderer init), but
   `init_renderer` (`pgraph.c`) now prints exactly the warning this item used to say was missing:
   a framed stderr banner plus *"VR requires Vulkan and IS INACTIVE this session"* whenever
   `vr.enable` was true and the renderer that failed was Vulkan, plus a persistent (non-fading)
   UI warning in the Display tab and Video Debug overlay. Verified only by a clean win64-cross
   build so far — nobody has yet deliberately broken VK init and watched the banner fire live,
   or confirmed the on-disk config still says VULKAN after a restart. That live exercise is now
   the concrete remaining gap, not the missing code.
3. **Tier-3 durable anchoring across reboots is unproven in these records.** The §9b pointer test
   defines the pass (head-look tracks after a rig reboot); its outcome is not recorded in the
   docs this spec consolidates — treat the OFP pointer profile as [ASSUMED]-durable until the
   reboot test is logged. Related: AOB mode has no field-confirmed title yet.
4. Smaller: panel interaction is a deliberate log-only stub (future Touch scope,
   `vr_sbc_panel.c:729-761`); `chains[1]`/stereo dormant; aspect ignores anamorphic/widescreen
   (plan U7); `xrEndSession` from non-STOPPING is a tolerated spec wart (plan §7 F2); the
   `xemu-vr` → PenguinBox string rebrand is pending (`AGENTS.md:213-216`).
