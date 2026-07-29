# Spec — Config system

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| B | `config_spec.yml`, `subprojects/genconfig/{gen_config.py,cnode.h}`, `ui/xemu-settings.{cc,h}`, `ui/xemu.c` (main), `system/vl.c` (arg synthesis), consumers of `g_config` throughout | v1 draft | 2026-07-27 |

Paths are repo-root-relative; `build/xemu-config.h` is a generated artifact
(not committed), its line numbers are from the current build tree.

## Purpose & scope

The config system is the single persistence mechanism for user-facing
PenguinBox settings: one schema (`config_spec.yml`), one generated C struct
(`struct config g_config`), one TOML file on disk. It covers schema →
generator → generated structures, the on-disk file and its per-platform
location, the load/save lifecycle, typing, defaults, CLI interaction, and the
operationally critical startup-once vs. live-read distinction. Out of scope: QEMU's own
`-readconfig`/QemuOpts machinery (untouched, not connected to `g_config`),
EEPROM/flash content, and controller-database internals.

## External interface

**The yml schema.** `config_spec.yml` (repo root, 638 lines) is a nested YAML
tree. Leaves are either shorthand `name: type` or a mapping with `type:` plus
optional `default:` (and `values:` for enums, `items:` for arrays). Types
accepted by the generator: `boolean`, `string`, `integer`, `number` (float),
`enum`, `array`, `table`, with aliases `bool/int/float/str`
(`subprojects/genconfig/gen_config.py:48-60`). Shorthand leaves get the
type's zero default: `false`, `''`, `0`, `0.0`, `[]`
(`gen_config.py:72-80`); an enum without `default:` gets its first listed
value (`gen_config.py:81-82`). This shorthand rule is why
`display.vulkan.validation_layers` — declared shorthand at
`config_spec.yml:473` — defaults to **false** (see I10 and Failure modes).

**The generator.** `meson.build:3765-3768` runs
`subprojects/genconfig/gen_config.py` on `config_spec.yml` at build time,
producing `build/xemu-config.h` (subproject wired at `meson.build:2466-2467`,
added to `common_ss` at `meson.build:4056`). The header contains (a) C enums +
`struct config` for all keys, includable from C; (b) under
`#ifdef DEFINE_CONFIG_TREE`, a C++ `CNode config_tree` literal mirroring the
schema with `offsetof` links into `struct config` and baked-in defaults
(`gen_config.py:202-266, 285-295`). Exactly one translation unit defines the
tree: `ui/xemu-settings.cc:37-38`. The runtime engine is
`subprojects/genconfig/cnode.h` (CNode: TOML overlay, struct store/load,
delta serialization), built on toml++ (`subprojects/tomlplusplus.wrap`,
pinned rev `30172438`; `meson.build:3695`).

**The on-disk TOML.** Filename is `xemu.toml` (`ui/xemu-settings.cc:42`).
Location resolution (`ui/xemu-settings.cc:51-105`): (1) explicit
`-config_path PATH` wins; (2) portable mode — if `xemu.toml` exists next to
the executable (`SDL_GetBasePath()`), that file is used; (3) otherwise
`SDL_GetPrefPath("xemu", "xemu")`. On Linux SDL3 pref path is
`$XDG_DATA_HOME/xemu/xemu/` (default `~/.local/share/xemu/xemu/xemu.toml`)
— note **XDG data dir, not** `~/.config` [DERIVED from SDL3 documented
semantics; not exercised on the rig, which always passes `-config_path`].
Windows: `%APPDATA%\xemu\xemu\xemu.toml`; macOS:
`~/Library/Application Support/xemu/xemu/xemu.toml` [ASSUMED, same SDL
semantics]. The resolved path is printed to stderr at startup
(`ui/xemu-settings.cc:70,90,103`). Rig practice: every launcher passes
`-config_path $HOME/xemu-assets/<name>.toml`
(`tools/perf/rig/ring-launch.sh:20`, `tools/vr/sb-launch.sh:31`,
`tools/vr/flat-launch.sh:49`).

**CLI flags.** Exactly two argv scans touch the config system — there is no
generic `-set section.key=value` mechanism:
- `-config_path PATH` (`ui/xemu.c:1360-1367`): selects the file, consumed
  before load, argv entries NULLed; `xemu_settings_set_path` asserts
  single-set before first path resolution (`ui/xemu-settings.cc:65-71`).
- `-dvd_path PATH` (`system/vl.c:3076-3087`): shadows
  `sys.files.dvd_path` for the session **without writing g_config** — it
  feeds the synthesized `-drive` argument only; never persisted.
All remaining argv is appended *after* the config-synthesized QEMU arguments
(`system/vl.c:3105-3108`) and parsed by QEMU's normal option parser; it can
influence the machine (e.g. a trailing `-m`) but never sets `g_config` keys.

**The C API** (`ui/xemu-settings.h`): `extern struct config g_config` (the
one global, defined `ui/xemu-settings.cc:40`); `xemu_settings_load()` /
`xemu_settings_save()`; `xemu_settings_set_path()` /
`xemu_settings_get_path()` / `xemu_settings_get_base_path()`;
`xemu_settings_get_error_message()`; `xemu_settings_set_string()` (free +
strdup helper for string fields, `ui/xemu-settings.h:66-71`); array editors
`add/remove_net_nat_forward_ports()`, gamepad-mapping load/reset. C code
reads settings by direct field access (`g_config.display.renderer`); the UI
binds ImGui widgets straight to field addresses
(`ui/xui/menubar.cc:189`, `ui/xui/main-menu.cc:762`).

## State & data structures

**`struct config`** (`build/xemu-config.h:104` onward): nested anonymous
structs matching the yml tree. Field mapping: boolean→`bool`;
integer→`int`; number→`float` (not double); string→`const char *`
(heap-owned, strdup'd); enum→`typedef int CONFIG_<PATH>` with 0-based values
in schema order plus a `_COUNT` sentinel (e.g. `CONFIG_DISPLAY_RENDERER_NULL
=0/OPENGL=1/VULKAN=2`, `build/xemu-config.h:13-19`); array→pointer +
`unsigned int <name>_count` (`gen_config.py:138-192`).

**`CNode config_tree`** (`cnode.h:68 ff.`): the runtime mirror. Each node
carries current value, default value, `serialized.offset` into
`struct config`, and integer/number min/max fields populated by
`cinteger`/`cnumber` (`cnode.h:727-739`) but **never enforced** (I8). Key
methods: `update_from_table()` (TOML overlay, `cnode.h:194-275`),
`store_to_struct()` (flatten to `g_config`, `cnode.h:406-441`),
`update_from_struct()` (reverse; used by save and array edits),
`differs_from_default()` + `generate_delta_toml()` (`cnode.h:523-690`),
`reset_to_defaults()`.

**Lifecycle.** `main()` (`ui/xemu.c:1320`): parse `-config_path` (1360) →
`xemu_settings_load()` (1370; parse failure → SDL error box + `exit(1)`,
1371-1378) → `atexit(xemu_settings_save)` (1379) → spawn the QEMU thread,
whose `qemu_init` synthesizes the machine command line from `g_config`
(`system/vl.c:2983-3103`: bootrom/avpack/skip_boot_anim → `-machine`;
eeprom → `-device smbus-storage`; flashrom → `-bios`; `mem_limit` →
`-m` via `((int)mem_limit+1)*64`, vl.c:3056; hdd/dvd → `-drive`) and prints
the result ("Created QEMU launch parameters:", vl.c:3110-3114). Load path
(`ui/xemu-settings.cc:158-207`): missing file → "Config file not found,
starting with default settings" and success; present file → read whole file,
`toml::parse`, `update_from_table` under `LC_NUMERIC=C`; **always** ends
with `config_tree.store_to_struct(&g_config)` (line 204), so `g_config` is
fully populated even when the file is absent. Save path
(`ui/xemu-settings.cc:209-238`): open `"wb"`, force
`input.allow_vibration=true` (deprecated-key migration, 225-228),
`update_from_struct(&g_config)`, write `generate_delta_toml()` — a **full
rewrite containing only keys that differ from defaults**.

## Invariants

- **I1** [DERIVED] After `xemu_settings_load()` returns, every key in
  `g_config` has a defined value — absent file, unknown keys, type errors,
  and invalid enum strings all resolve to the compiled-in spec default;
  no key is ever "unset" (`ui/xemu-settings.cc:204`; `cnode.h:204-217`).
- **I2** [DERIVED] The TOML file is read exactly once per process, before
  the QEMU thread exists; nothing re-reads it mid-run
  (`ui/xemu.c:1370` is the sole `xemu_settings_load` call site).
- **I3** [DERIVED] Precedence per key: compiled-in default < TOML value <
  runtime writes to `g_config` (UI/engine). CLI never sets a config key;
  its only levers are `-config_path` (file selection, `ui/xemu.c:1360`)
  and `-dvd_path` (session-local shadow that bypasses `g_config`,
  `system/vl.c:3076-3087`).
- **I4** [DERIVED] Save (`atexit`, `ui/xemu.c:1379`) rewrites the entire
  file with delta-from-default content only. Comments, formatting, and
  unrecognized keys in the previous file are destroyed; keys equal to
  default are removed (`cnode.h:588-690`, `ui/xemu-settings.cc:231`).
- **I5** [DERIVED] TOML syntax errors are fatal at startup (dialog +
  `exit(1)`, `ui/xemu.c:1370-1378`); key-level problems are non-fatal
  stderr messages with the key keeping its default — unknown key
  ("Warning: unrecognized key…", `cnode.h:205-210`), wrong type ("Error:
  incorrect type…", `cnode.h:212-217`), invalid enum string ("Error:
  invalid value…", `cnode.h:333-339`).
- **I6** [DERIVED] String fields in `g_config` are never NULL — every
  string is strdup'd from the tree, empty-string default included
  (`cnode.h:433`).
- **I7** [VERIFIED — generated artifact inspected] Enum fields are plain
  `int`s numbered 0..N-1 in schema order; the TOML representation is the
  string form (`build/xemu-config.h:13-19`; `cnode.h:429-430,672-676`).
- **I8** [DERIVED] The config layer performs no range enforcement: CNode
  integer/number min/max are stored but never checked; any clamping is
  consumer-side (e.g. `surface_scale` clamped ≥1 by its setter,
  `hw/xbox/nv2a/pgraph/vk/surface.c:38`, not by load).
- **I9** [DERIVED] The emulator writes `g_config` back during normal
  operation — renderer fallback (`pgraph.c:296,349`),
  `preferred_physical_device` autofill
  (`hw/xbox/nv2a/pgraph/vk/instance.c:443`), window
  `last_width/last_height` (`ui/xemu.c:533`), `last_viewed_menu_index`,
  eeprom-path autofill (`system/vl.c:2915`) — so the file on disk can
  change after a run with zero user edits.
- **I10** [VERIFIED] `display.vulkan.validation_layers` defaults to
  **false**: shorthand declaration `config_spec.yml:473`, shorthand
  default rule `gen_config.py:72-80`, baked default visible at
  `build/xemu-config.h:962-963` (`"validation_layers", false`). Unset ⇒
  the Vulkan validation detector is unarmed (see Failure modes / G1).
- **I11** [DERIVED] Editing the TOML while PenguinBox runs has no effect
  on the session (I2) and the edit is **overwritten at exit** by the
  delta rewrite (I4). The only supported external-edit flow is: quit →
  edit → relaunch.

## Policies & decision points

- **P1 Renderer selection & fallback — fixed 2026-07-28** [VERIFIED]: at
  context init, if the configured renderer isn't compiled in,
  `g_config.display.renderer` is overwritten with the default (OPENGL
  preferred over VULKAN, `pgraph.c:277-300`); if init *fails*, it retries
  the default and again writes the key back (`pgraph.c:346-357`) — that
  mutation is structurally required, since `pgraph_process_pending`'s
  switch-detection compares this same field against the active renderer
  every pump (I9 still holds: the emulator legitimately writes `g_config`
  back during normal operation). What no longer holds: this mutation used
  to **persist** past the session, because `xemu_settings_save()` simply
  serialized whatever `g_config.display.renderer` held at exit — one
  transient VK failure silently converted the config to OpenGL for all
  future runs (the H-3 hazard, CLAUDE.md/AGENTS.md). Now,
  `xemu_settings_note_requested_renderer()` records the value actually
  requested at the top of `init_renderer`, separately from the session's
  active value, and `xemu_settings_save()` restores it immediately before
  serializing (safe: that function's only call site is `atexit`, so nothing
  reads `g_config` afterward). A transient failure retries the real setting
  on next launch instead of being stuck on the fallback forever.
- **P2 Live renderer switch** [DERIVED]: `pgraph_process_pending` compares
  `g_config.display.renderer` against the active renderer every pump and
  starts a full switch on mismatch (`pgraph.c:3416-3422`). Hence renderer
  is a *live* key (UI combo binds the field directly,
  `ui/xui/main-menu.cc:762`), and a switch re-runs renderer init — which
  re-reads all `display.vulkan.*` keys.
- **P3 Validation-layer arming chain** [DERIVED]:
  `debug_utils_extension_enabled = validation_layers && debug-utils ext
  available` (`instance.c:142-146`) → `enable_validation =
  g_config.display.vulkan.validation_layers` (`instance.c:205`) → if
  enabled and `VK_LAYER_KHRONOS_validation` is enumerable, layers +
  `VkValidationFeaturesEXT` (sync validation) attach to
  `VkInstanceCreateInfo` and the banner "Warning: Validation layers
  enabled. Expect performance impact." prints (`instance.c:216-224`);
  else "Warning: validation layers not available" and validation is
  dropped (`instance.c:225-228`). Messenger (VERBOSE|WARNING|ERROR, all
  types → stderr `[vk]` prefix) is created only if
  `debug_utils_extension_enabled` (`instance.c:241-253`, callback
  45-57). `assert_on_validation_msg` escalates warnings/errors to an
  abort (`instance.c:55`).
- **P4 VR gating** [DERIVED]: `xemu_vr_wanted()` = `vr.enable &&
  renderer==VULKAN` (`vr_manager.c:27-31`), consulted only at VK instance
  init (`instance.c:634`); instance creation reroutes through
  `xrCreateVulkanInstanceKHR` when the bootstrap is active
  (`instance.c:233`, `vr_manager.c:67-81`). `vr.enable` is therefore
  *not* live — it takes effect at next VK renderer init (restart, or a
  renderer bounce via P2).
- **P5 Physical device choice** [DERIVED]: an active OpenXR runtime
  dictates the GPU (spec requirement, overrides config,
  `instance.c:377-393`); otherwise exact-name match against
  `preferred_physical_device`, else first compatible device, and the
  chosen name is written back to the key (`instance.c:408-445`).
- **P6 Portable mode** [DERIVED]: `xemu.toml` beside the executable flips
  the whole base path (config + default eeprom) to the install dir
  (`ui/xemu-settings.cc:51-63,80-89`); `-config_path` overrides the
  config file but *not* the base path used for `eeprom.bin`.
- **P7 Locale discipline** [DERIVED]: load and save force `LC_NUMERIC=C`
  around parse/print so floats always use `.` radix
  (`ui/xemu-settings.cc:171-193,217-237`).
- **P8 Startup-once vs live** [DERIVED] — the operational split. *No key
  is file-live* (I2/I11); "live" means the in-memory field is consulted
  after startup, so the in-process UI can change it without restart.
  Startup-once (restart required): all `sys.*` and boot media (consumed
  into synthesized argv, `vl.c:2983-3103`), `perf.hard_fpu` (TCG init,
  `target/i386/tcg/translate.c:4231`), `audio.vp.num_workers` (worker
  pool, `vp.c:1771`), `display.window.startup_size/fullscreen_on_startup`
  (`ui/xemu.c:1054-1059,1161`), `general.show_welcome/updates.check`
  (`ui/xui/main.cc:154`), `display.setup_nvidia_profile`
  (`ui/xemu.c:1382-1386`, Windows). Live: `display.renderer` (P2);
  `display.vulkan.*` (each VK init — live across a renderer bounce);
  `display.quality.surface_scale` (read at surface creation,
  `vk/surface.c:76`); every `display.ui.*` and `display.debug.video.*`
  key (per-frame, `ui/xui/main.cc:231-270`); `display.window.vsync`
  (polled per frame with change detection, `ui/xui/main.cc:340-343`);
  `audio.use_dsp/use_dsp_jit` (polled per APU frame,
  `gp_ep.c:26-49` via `apu.c:221`); `audio.hrtf` per voice
  (`vp.c:1458`); `audio.volume_limit` (`apu/monitor.c:77`);
  `perf.cache_shaders` per shader-cache op (`gl/shaders.c:282,479,795`);
  `net.enable` (startup read `vl.c:2871` *and* runtime toggle via UI →
  `xemu_net_enable/disable`, `ui/xui/main-menu.cc:983-991`); all
  `vr.headlook_*`, `vr.screen_*`, `vr.sbc_panel_*` (per-frame in
  `vr_camera.c:100-506`, `vr_xr_compositor.c:1346-1500`); input bindings
  (pointer tables consulted on connect/rebind,
  `ui/xemu-input.c:100-120`).
- **P9 Per-title practice** [VERIFIED — operational record]: the code
  supports exactly one config file per invocation; nothing selects
  configs per title. Rig convention layers one *shared* file over that
  (`$HOME/xemu-assets/sb-floor-s1.toml` in launchers,
  `tools/perf/rig/ring-launch.sh:20`). `docs/perf/tool-specs.md:291`
  (T8) proposes per-title `configs/<title>.toml` + `.env`; not built.

## Failure modes

- **G1 — validation detector dead (OPEN, unfixed).** Default-false
  (I10) means an unset key silently disarms Vulkan validation: every
  "zero validation errors" claim made without confirming the key was an
  unarmed-detector claim (`docs/perf/tooling-gaps.md:14-44`, G1/G2).
  Worse: as of 2026-07-27, setting it **true** on the rig with the
  Khronos layer installed still produced zero validation output —
  plumbing suspect, never diagnosed. Debug entry points, in order: does
  the banner (`instance.c:219`) appear in the log? If not, the key or
  layer enumeration (`instance.c:58-85`) failed. If yes, check the
  messenger chain: debug-utils extension gating (`instance.c:142-146`
  — note it requires the *extension*, distinct from the layer) and
  messenger creation (`instance.c:241-253`). Prime structural suspect:
  the VR seam — with VR active the instance is created by
  `xrCreateVulkanInstanceKHR` (`instance.c:233`), and runtime handling
  of `ppEnabledLayerNames`/`pNext` is unaudited. Bypass test:
  `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LOADER_DEBUG=all`
  per G1.
- **One config, many titles.** Because P9 offers no per-title selection,
  a toml tuned for one game silently serves another: the SB config
  (`sb-floor-s1.toml`) served Line of Contact for an entire campaign —
  including an SB-specific input map whose scancode-18 collision blocked
  reaching gameplay (`docs/perf/tooling-gaps.md:206-223` G10;
  `docs/perf/loc/baseline-log-excerpt.txt:7` shows the path in the
  actual boot log). The stderr `config path:` line is the only tell.
- **Fatal load cases**: TOML syntax error, or file present but
  unreadable → error dialog + `exit(1)` (`ui/xemu.c:1370-1378`,
  `ui/xemu-settings.cc:182-201`). Missing file is *not* an error.
- **Silent key loss**: a typo'd key warns once on stderr (I5), then is
  destroyed by the exit rewrite (I4) — the evidence of the mistake
  deletes itself.
- **Persisted fallback**: P1 + I9 + I4 = one failed VK init permanently
  rewrites `display.renderer = OPENGL` into the toml. Detect loudly
  (H-3): grep the log for "Switched to default renderer".
- **Lost external edits**: any toml edit made while a session runs is
  clobbered at exit (I11).
- **Save failure**: unwritable path at exit → one stderr line, session
  changes lost (`ui/xemu-settings.cc:211-215`).

## Instrumentation

All config-system observability is stderr/stdout lines; grep targets:
`xemu_settings_set_path: config path:` / `…get_base_path: base path:` /
`…get_path: config path:` (`ui/xemu-settings.cc:70,90,103`) — proves
*which file* loaded, the P9 hazard check; "Config file not found…"
(`ui/xemu-settings.cc:164`); "Warning: unrecognized key '…' at line L
column C" / "Error: incorrect type…" / "Error: invalid value…"
(`cnode.h:206,213,335`); "Created QEMU launch parameters: …"
(`system/vl.c:3110-3114`) — the effective `sys.*` config; "Warning:
Validation layers enabled. Expect performance impact."
(`instance.c:219-220`) — **the armed-detector banner**; perf report
scripts must refuse to count validation errors unless this banner is in
the log (tooling-gaps G2 rule); "Warning: validation layers not
available" (`instance.c:226`); "desired validation layer not found: …"
(`instance.c:78-79`); `[vk] <message>` (`instance.c:50`) — validation
output when the chain works; "Warning: Configured renderer unavailable.
Switching to %s." (`pgraph.c:297-299`) — the compile-time-substitution
tell; the P1 runtime-fallback tell is now a framed
`==== RENDERER FALLBACK ====` stderr banner plus a queued notification
(`pgraph.c`, `init_renderer`, fixed 2026-07-28), and — unlike every other
line in this list — a **persistent**, non-fading signal:
`nv2a_get_renderer_fallback_active()` drives a warning row in both the
Display settings tab and the Video Debug overlay for as long as the
session runs on the fallback.
No counters, no in-tree unit tests referencing `xemu_settings`, no dump
of the merged effective config.

## Verified-by

- Generated-artifact inspection: `build/xemu-config.h:962-963` baked
  `"validation_layers", false` (I10); enum layout at lines 13-19 (I7).
- Campaign record: `docs/perf/tooling-gaps.md` G1/G2 (unarmed detector,
  and set-true-still-silent as of 2026-07-27) and G10 +
  `docs/perf/loc/baseline-log-excerpt.txt:7` (SB toml serving LoC) —
  negative verification of Failure modes 1-2.
- Rig boot logs show the path lines and "Created QEMU launch parameters"
  (same excerpt file), confirming load order and synthesis end-to-end.
- Everything else is code-trace only; no deliberate experiment has
  exercised I3-I8, I11, or P5-P7 (hence [DERIVED]).

## Confidence & unknowns

Tags are inline: [VERIFIED] I7, I10, P9, and the G1/G10 records; the
rest [DERIVED]; pref-paths partially [ASSUMED]. Unknowns, by priority:
1. **Why validation output is absent with the key true** (G1). The code
   chain reads correct; the XR-created-instance seam (`instance.c:233`)
   and the debug-utils-extension gate (`instance.c:142`) are the two
   unaudited links. Until diagnosed, no "0 validation errors" claim is
   admissible without the armed banner in the same log.
2. **Trailing-argv override semantics** [ASSUMED]: user argv appended
   after synthesized args (`vl.c:3105-3108`) *probably* lets a trailing
   `-m`/`-machine` override config-derived values via QemuOpts merge
   rules — never exercised; do not rely on it.
3. **SDL pref-path per platform** [DERIVED/ASSUMED]: the XDG *data*-dir
   location is from SDL3 docs, unverified here (rig always passes
   `-config_path`); verify before writing any user-facing install doc.
4. Whether a renderer bounce (P2) fully re-arms validation and VR
   (`display.vulkan.*`, `vr.enable` re-read at re-init): traced, never
   deliberately tested — the cheapest restart-free validation toggle if
   it works.
