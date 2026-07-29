# Spec — GL renderer (parity notes)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| B | `hw/xbox/nv2a/pgraph/gl/` | v1 draft (parity notes) | 2026-07-27 |

## Purpose & scope

The OpenGL backend is the **parallel NV2A implementation** next to `vk/` — the
original xemu renderer, ~6,226 lines (`wc -l gl/*.c gl/*.h`) vs ~22,253 in
`vk/`. In this fork it has exactly two jobs:

1. **Parity reference.** Every default-path claim we make about VK ("output
   identical with lever off", "byte-identical default behavior") is implicitly
   a claim of agreement with the semantics both backends inherited. The one
   cross-backend fix we have shipped (the `possibly_dirty` hash leak, §Invariants
   I4) was found *by* comparing the trees. [VERIFIED — vk/texture.c:2503-2518
   comment cites gl/texture.c behavior as the reference]
2. **Fallback renderer.** It is what the stock selection machinery prefers and
   what a failed Vulkan init silently lands on (§Policies).

This is a PARITY-NOTES spec, not a full behavioral spec of `gl/` internals:
it covers selection/fallback, the gl↔vk structural map, the shared
dirty-tracking contract, and the invariants both backends must uphold. The
contract going forward: **all perf and VR work happens in `vk/`; `gl/` is
touched only to keep parity or read as reference.** [DERIVED — from the
env-gate census in §State: zero fork levers exist in `gl/`]

## External interface

- **Renderer vtable.** `gl/` implements `PGRAPHRenderer.ops`
  (pgraph.h:108-136): init/finalize, draw begin/end, flip_stall, flush_draw,
  surface_update, image_blit, get_report, pre_savevm/pre_shutdown
  trigger+wait pairs, process_pending, get_framebuffer_surface,
  surface-scale get/set, get_gpu_properties. The table is
  `pgraph_gl_renderer` (gl/renderer.c:187-212), `.type =
  CONFIG_DISPLAY_RENDERER_OPENGL`, `.name = "OpenGL"`, registered by an
  `__attribute__((constructor))` (gl/renderer.c:214-217). [VERIFIED]
- **Selection input.** `g_config.display.renderer`, enum
  `["NULL", OPENGL, VULKAN]`, **default OPENGL** (config_spec.yml:468-471).
  [VERIFIED]
- **Thread/context contract.** `early_context_init` runs on the main/UI
  thread (ui/xemu.c:1130 → `nv2a_context_init`, pgraph.c:293-314) and
  creates two shared GLO contexts, render + display (gl/renderer.c:27-43).
  `ops.init` runs on the pfifo thread (pfifo.c:457 → `pgraph_init_thread`,
  pgraph.c:364-367). [VERIFIED]
- **Failure interface asymmetry.** `pgraph_gl_init` takes `Error **errp` but
  never sets it; required GL extensions are hard `assert()`s
  (gl/renderer.c:45-62). VK's init reports failure properly via `error_setg`
  (vk/instance.c:155,165,235,400,438,536,573). Consequence: graceful fallback
  only flows **VK→GL**, never GL→VK. [VERIFIED]

## State & data structures (brief: the gl/ file map vs vk/)

Backend-local state root: `PGRAPHGLState *pg->gl_renderer_state`
(gl/renderer.c:49-50). Parallel map, by same-name file:

| Subsystem | gl/ | vk/ | Both? |
|---|---|---|---|
| Renderer lifecycle, flush | renderer.c (217) | renderer.c (449) | both |
| Surface cache | surface.c (1,489) | surface.c (2,378) + surface-compute.c | both (+VK compute path) |
| Texture cache, possibly_dirty | texture.c (828) | texture.c (2,991) | both |
| Shader compile/bind | shaders.c (860) | shaders.c (835) + glsl.c (SPIR-V) | both |
| Draw dispatch | draw.c (490) | draw.c (3,002) | both |
| Present/display | display.c (440) | display.c (1,712) | both |
| Blit, vertex, reports, gpuprops, debug, constants | one file each | one file each | both |

Shader **translation** (vsh/psh → GLSL text) is shared front-end code in
`pgraph/glsl/` (common.c, vsh.c, psh.c, geom.c, shaders.c) consumed by both
backends; only compile/link/bind is per-backend. [VERIFIED — directory listing]

**VK-only, no gl/ counterpart:** buffer.c (staging/in-batch uploads),
command.c, image.c (layout model), instance.c (device bring-up + VR seams),
surface-compute.c, glsl.c/h (SPIR-V), `openxr/`, and the whole VR subsystem
`vr_manager.c`, `vr_camera.c`, `vr_xr_{loader,session,compositor}.c`,
`vr_sbc_panel.c`, `vr*.h`. [VERIFIED — listing; no `vr_*` under gl/]

**Env-gate census (the decisive fact):** `grep -rn getenv gl/` → **zero
hits**. Every 2026-07 perf lever is VK-only: `XEMU_SURF2TEX_EXT`
(vk/texture.c:1299), `XEMU_SURF2TEX_ZEROCOPY` (:1327),
`XEMU_SURF2TEX_FEEDBACK` (:1351), `XEMU_ALIAS_SURFACES` (vk/surface.c:1117),
`XEMU_FRAME_OVERLAP` (vk/draw.c:66), `XEMU_ASYNC_PRESENT` (vk/display.c:259),
`XEMU_ASYNC_QUERIES` (vk/reports.c:545), `XEMU_QUERY_RING` (vk/reports.c:30),
`XEMU_BATCH_QRESET` (vk/reports.c:590), `XEMU_NARROW_FENCE`
(vk/surface.c:965), `XEMU_NO_PREDICTIVE_READBACK` (vk/surface.c:935),
`XEMU_EXACT_DIRTY` (vk/texture.c:440), `XEMU_INBATCH_UPLOADS`
(vk/buffer.c:56), `XEMU_PIPE_OPT` (vk/draw.c:38), `XEMU_CONST_SKIP`
(vk/shaders.c:562), `XEMU_NO_TEX_DIRTY_CLEAR` (vk/texture.c:2522). Under GL
these variables are silently inert. [VERIFIED 2026-07-27]

## Invariants (numbered; tagged)

Honesty note: **none of these has a cross-backend differential test today.**
Tags reflect code-reading evidence only.

- **I1 — Default-path output parity.** For the same guest command stream with
  all levers at defaults, GL and VK must produce the same guest-visible
  behavior (rendered output, report values, blit results). This is the
  regression gate behind every "output-identical" lever claim. [ASSUMED —
  stated contract; no automated comparison exists]
- **I2 — Shared dirty channel.** Both backends detect CPU writes to texture
  memory via the same `DIRTY_MEMORY_NV2A_TEX` page bitmap, read with
  `memory_region_test_and_clear_dirty` (gl/texture.c:80-87,
  vk/texture.c:577-578). Because the read *clears* the bitmap, a positive
  result must be transferred into per-node `possibly_dirty` flags
  (gl/renderer.h:159, vk/renderer.h:266) or dirtiness is lost — losing the
  transfer is a correctness bug, not a perf bug. [VERIFIED mechanism;
  DERIVED contract]
- **I3 — Reuse-without-rehash conditions.** A cached texture may be bound
  without recomputing its content hash only when (a) its node is not flagged
  `possibly_dirty` AND its pages are clean in the bitmap
  (gl/texture.c:331-340, vk/texture.c:2419-2428), or (b) the bind is
  surface-backed, where freshness is keyed by `draw_time` comparison and
  content is never hashed at all (gl/texture.c:347 `!surf_to_tex` guard;
  vk/texture.c:2435 `!surface_to_texture`). Otherwise the hash MUST be
  recomputed and compared before reuse; the expensive rebuild/upload happens
  only if the hash actually changed (gl/texture.c:355-361 destroy+recreate;
  vk/texture.c:2496-2500 in-place re-upload). [VERIFIED both trees]
- **I4 — Serviced flags are consumed.** A bind that rehashed under
  `possibly_dirty` must clear the node flag, on cache HIT and MISS alike. GL
  always did this (gl/texture.c:374 unconditional clear). VK historically
  cleared only on MISS (vk/texture.c:2537), so one whole-VRAM mark condemned
  a node to rehash on every bind forever — the hash-leak found by backend
  comparison, fixed at vk/texture.c:2503-2527 (default ON;
  `XEMU_NO_TEX_DIRTY_CLEAR=1` restores the leak for A/B). GL is the semantic
  reference here. [VERIFIED]
- **I5 — Flush invalidates everything.** Renderer flush (loadvm, scale
  change, renderer switch) must whole-VRAM-mark every cached texture
  possibly-dirty: gl/renderer.c:107-116 (`pgraph_gl_flush`),
  vk/renderer.c:91-114 (`pgraph_vk_flush`, explicitly page-granular,
  `use_exact_spans=false`). [VERIFIED]
- **I6 — Surface→texture correctness.** A texture bind aliasing a live
  render target must observe the latest rendered content without a VRAM
  round trip (GL: render_surface_to_texture keyed by `draw_time`,
  gl/texture.c:291-298, 378-385; VK: the S2T path family keyed by
  draw_time/frame_time, vk/texture.c:2455-2494). Non-S2T binds overlapping a
  dirty surface must force the surface download *first* so the bytes being
  hashed are current (gl/texture.c:301-314). [VERIFIED in GL; DERIVED that
  VK's equivalent preserves the same ordering]
- **I7 — Persistence duties.** Both backends must download dirty surfaces
  before savevm and write back the shader cache before shutdown via the
  pre_savevm/pre_shutdown trigger+wait ops (gl/renderer.c:152-186).
  [DERIVED — op symmetry; not experimentally compared]

## Policies & decision points (renderer selection; fallback)

- **Stock preference is OPENGL, twice over.** Config default is OPENGL
  (config_spec.yml:471), and `get_default_renderer()` checks
  `renderers[CONFIG_DISPLAY_RENDERER_OPENGL]` **first**, VULKAN second, NULL
  last (pgraph.c:277-291). [VERIFIED]
- **Compile-time substitution.** If the configured renderer isn't built in,
  `nv2a_context_init` swaps in the default with only an fprintf warning
  (pgraph.c:295-300). [VERIFIED]
- **Runtime fallback is loud, and does not persist (fixed 2026-07-28).**
  `init_renderer` (pgraph.c:341-400ish, grown by the fix): if
  `attempt_renderer_init` fails on the configured renderer (VK's `ops.init`
  set an error → queued error message, pgraph.c:330-336), it mutates
  `g_config.display.renderer` to the default **for this session's active
  renderer only** — that mutation is still necessary, since
  `pgraph_process_pending`'s switch-detection compares
  `g_config.display.renderer` against `pg->renderer->type` every pump
  (pgraph.c:3416) and would otherwise loop retrying forever. What changed:
  (a) a framed, unmissable stderr banner plus a queued notification, both
  naming the fallback renderer and stating explicitly that the saved setting
  has not changed; (b) `xemu_settings_note_requested_renderer()`
  (`ui/xemu-settings.{h,cc}`) records the value that was actually
  *requested*, separately from the session's active value, and
  `xemu_settings_save()` restores it immediately before serializing — since
  that function's only call site is `atexit`, the mutation is safe and one
  transient failure no longer converts the on-disk config to the fallback
  permanently; (c) a persistent (not fading) red warning line in both the
  Display settings tab and the Video Debug overlay (`nv2a-renderer-status.h`
  exposes `nv2a_get_renderer_fallback_active/reason()`), visible for as long
  as the session runs on the fallback, not just the few seconds a toast is
  on screen. If both the configured and default renderers fail: `exit(1)`
  (unchanged). [VERIFIED — win64-cross build, GCC 16, clean]
- **The VR consequence — now surfaced.** `xemu_vr_wanted()` = `vr.enable &&
  renderer == VULKAN` (vk/vr_manager.c:28-31) is unchanged: a failed VK init
  still makes VR "not wanted," no abort, game runs flat. What changed is
  that the fallback banner above now explicitly appends *"VR requires
  Vulkan and IS INACTIVE this session"* whenever `vr.enable` was true and
  the renderer that failed was Vulkan, both to stderr (with an additional
  dedicated line) and in the persistent UI warning — closing the "healthy
  toast, silently no VR" gap this section used to describe. H-3 enforcement
  is still primarily operational (force `display.renderer = 'VULKAN'` in
  config), but a violation is no longer silent if it happens.
  [VERIFIED, 2026-07-28]
- **Runtime renderer switch.** Changing `display.renderer` while running
  triggers a phased switch in `pgraph_process_pending` (pgraph.c:3413-3459):
  park CPU0, flush, finalize old, `init_renderer` new — which can itself
  fall back per the above. [VERIFIED]

## Failure modes

- **VK→GL fallback under vr.enable — no longer silent (fixed 2026-07-28):**
  headset still stays dark and all `XEMU_*` levers are still inert, but the
  fallback now prints a framed stderr banner, an explicit VR-inactive line,
  a queued notification, and a persistent red warning in both the Display
  tab and the Video Debug overlay for as long as the session runs on the
  fallback. Still worth asserting the renderer name in logs for scripted
  rig runs (a UI warning doesn't help an unattended script), but a human at
  the keyboard can no longer miss this. [VERIFIED — win64-cross build clean]
- **GL init cannot fail gracefully:** missing `GL_EXT_texture_compression_s3tc`
  or `GL_ARB_ES2_compatibility` is an assert-abort (gl/renderer.c:60-62), and
  `errp` is never set, so nothing falls back *from* GL. [VERIFIED]
- **Both renderers fail → `exit(1)`** (pgraph.c:360-361); the "FIXME: Try
  others" (pgraph.c:358) means NULL is never auto-tried. [VERIFIED]
- **Flush asymmetry (latent parity risk):** VK's flush also sets
  `pg->texture_dirty[0..3] = true` (vk/renderer.c:107-109); GL's flush does
  not, so GL's registers-unchanged fast path (gl/texture.c:257-286) relies
  on the bitmap/flag mark alone after loadvm. Divergent mechanism; guest
  impact unassessed. [VERIFIED difference; consequence UNKNOWN]

## Instrumentation

- GL carries the **shared upstream** `NV2A_PROF_*` counters (e.g.
  gl/texture.c:430 TEX_UPLOAD, gl/shaders.c:793 SHADER_GEN, gl/draw.c:365
  QUERY) and ~20 trace events in texture.c/surface.c. [VERIFIED]
- **Fork instrumentation is absent from gl/:** no `NV2A_PROF_TEX_HASH{,_KB}`,
  no `NV2A_PHASE_LEAF` phase timers, no S2T debug dumps, no env-gated
  logging (all grep-clean). A/B comparisons against GL are therefore limited
  to shared counters plus guest-visible output. [VERIFIED 2026-07-27]

## Verified-by

- Static reads, 2026-07-27: pgraph.c:233-362, 3400-3459; gl/renderer.c full;
  gl/texture.c:33-110, 240-390, 770-790; vk/texture.c:2400-2545,
  vk/renderer.c:90-115; vk/vr_manager.c:1-60; config_spec.yml:468-471.
- Greps, 2026-07-27: `getenv` census (gl/ = 0 hits, vk/ ≈ 40);
  `possibly_dirty` in both trees; `error_setg` in vk/instance.c.
- **The loud-fallback fix (2026-07-28)** compiled clean under the win64-cross
  build (GCC 16, `-Werror`-grade on implicit declarations); it has not yet
  been exercised by deliberately breaking VK init and confirming the banner
  fires live and the on-disk config stays at the requested renderer across a
  restart — the natural next experiment, and the closest thing this spec has
  to a runtime test of the fallback path at all.
- **Still no runtime verification of anything else here**: no GL-vs-VK
  output diff has ever been run. The natural first experiment if this spec
  is promoted past parity notes.

## Confidence & unknowns

Selection/fallback machinery and the possibly_dirty contract: **high
confidence** (small, fully read). Everything tagged [DERIVED]/[ASSUMED]:
medium — code-symmetry arguments without experiments. Top unknowns:

1. **No cross-backend differential test exists** — I1 (output parity) is
   enforced by nothing; a screenshot/report-value diff harness on a stock
   title would convert most [ASSUMED] tags.
2. **GL's current health in this fork is untested** — nothing confirms the
   GL backend still boots SB/LoC-era titles after a year of VK-side change to
   shared pgraph front-end code.
3. **The flush `texture_dirty[]` asymmetry** (§Failure modes) and the GL
   fast-path's bitmap-only check after loadvm — is there a stale-texture
   window GL has and VK doesn't? Unassessed.
