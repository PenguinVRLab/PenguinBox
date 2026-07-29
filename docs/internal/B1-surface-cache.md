# Spec — VK surface cache

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| Tier | B (inherited xemu, heavily modified by us) |
| Code | `hw/xbox/nv2a/pgraph/vk/surface.c` (~2,380 lines), struct in `vk/renderer.h:125` |
| Status | v1 draft |
| Last verified | 2026-07-27 (LoC + SB campaigns; tags per claim) |

## Purpose & scope

The surface cache maintains the correspondence between **guest-RAM regions the
NV2A renders into** (color and zeta render targets) and **host `VkImage`s**.
It decides when a guest region becomes a GPU surface, when GPU content must be
written back to guest RAM (download), when guest RAM must be re-read into the
GPU image (upload), and when a surface ceases to exist. It is the single
authority on render-target residency; the texture cache (spec B2) *consumes*
surfaces through lookup functions but never creates or destroys them.

Out of scope here: surface-as-texture binding semantics (B2), the image-layout
choreography (B5), format decode chains (B3).

## External interface

**Env flags** (getenv-once; process lifetime):
- `XEMU_ALIAS_SURFACES` / `XEMU_NO_ALIAS_SURFACES` — alias residency mode
  (`surface.c:1110`, gate `alias_surfaces_enabled()`; NO_ wins; default OFF;
  OFF path is byte-identical to the historical evict-on-overlap flow
  [VERIFIED — SB gate: default-flags run byte-identical pre/post merge]).
  Announces itself on stderr when ON (armed-detector convention).
- `XEMU_SURF_DUMP`, `XEMU_SURF_ACCESS_LOG`, `XEMU_SURF_ATTR` — instrumentation
  (spec A3).
- Related levers that *touch* this machinery but are specced in A2:
  `XEMU_NARROW_FENCE` (per-surface submit fencing), predictive readback
  (`XEMU_NO_PREDICTIVE_READBACK`, `XEMU_PREDL_TOGGLE_FILE`).

**In-tree API** (called by pgraph/texture/draw code):
- `pgraph_vk_surface_update(d, upload, color_write, zeta_write)`
  (`surface.c:2196`) — the target-switch entry point; runs on surface-format
  method state changes.
- `pgraph_vk_surface_get(d, addr)` (`surface.c:1195`) — exact-base lookup;
  returns the **newest** binding at that base (list order, see I4).
- `pgraph_vk_surface_get_within(d, addr)` — containing-range lookup (used by
  texture binds).
- Downloads: `pgraph_vk_surface_download_if_dirty` (`:1438`),
  `pgraph_vk_process_pending_downloads` (`:698`, flip-time),
  `pgraph_vk_download_dirty_surfaces` (`:721`),
  `pgraph_vk_download_surfaces_in_range_if_dirty` (`:199`, memcb path).
- Upload: `pgraph_vk_upload_surface_data` (`:1445`).
- Layout/feedback (B5's machinery, hosted here):
  `pgraph_vk_surface_transition` (`:143`), `pgraph_vk_surface_rest_layout`
  (`:155`), `pgraph_vk_surface_feedback_flush` (`:171`).
- Scaling: `pgraph_vk_set_surface_scale_factor` (`:36`) — full cache flush +
  rebuild.
- `pgraph_vk_surface_flush` (`:2356`) — download-dirty + invalidate ALL
  (used by scale change, savevm-adjacent paths).

**Guest-visible surface:** none directly — behavior is only observable as
rendering correctness and as guest-RAM contents after downloads.

## State & data structures

`SurfaceBinding` (`renderer.h:125-192`) — one per resident surface, on the
`r->surfaces` QTAILQ (plus a second list `r->invalid_surfaces`, see below).
Field semantics (the ones that carry invariants):

- `vram_addr` — guest phys base = DMA object address + surface offset
  (`surface.c:1940`).
- `width`, `height`, `pitch` — geometry in **guest pixels** (scaling multiplies
  the VkImage extent, not these).
- `size` — **`height × MAX(pitch, width × bytes_per_pixel)`**
  (`surface.c:1945`). Every row counts a full pitch, including the last. All
  overlap math uses this byte extent [VERIFIED — read directly; the LoC alias
  ratio tests depend on it].
- `color` — color vs zeta target. `swizzle` — surface *type* is swizzled
  (`NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE`), i.e. the guest-RAM byte layout is
  Morton order (B3), not linear rows.
- `frame_time` — stamped from `pg->frame_time` at creation; `pg->frame_time`
  advances **once per flip** (`pgraph.c:928`). `draw_time` — stamped from
  `pg->draw_time`, which advances **per draw**. These are the two dedup/freshness
  clocks; every consumer must pick the one matching its cadence (B2's
  frame-vs-draw rule) [VERIFIED — the LoC 700→2 copy collapse was exactly this].
- `draw_dirty` — GPU has rendered content not yet downloaded.
  `download_pending` / `upload_pending` — deferred work flags.
- `feedback_mode` (v9, sticky per surface lifetime) — surface is sampled while
  bound as render target; rests in `VK_IMAGE_LAYOUT_GENERAL` (B5).
- `flushed_draw_time` (v9) — last draw_time already covered by a feedback
  visibility flush; the once-per-redraw dedup (a per-bind flush measurably
  doubled render-pass count [VERIFIED — LoC ~700→1500 rpass regression,
  reverted]).
- `image_layout` — tracked actual layout of `image`; ALL transitions go
  through `pgraph_vk_surface_transition` (B5's core invariant).
- `last_write_submit` / `last_use_submit` — submit-counter fencing: the
  submission that last **wrote** the image vs last **read** it (zero-copy
  texture sampling). Eviction/overwrite must wait on BOTH
  (`renderer.h:161-167`, `pgraph_vk_wait_for_surface_write :564`).
- `cpu_read_frame`, `predl_frame`, `reads_frame`, `dl_frame` — predictive
  readback + per-frame attribution counters (A2/A3).
- `image`, `image_view`, `allocation`, `image_scratch` (+ its layout) — VK
  resources; scratch is the scaling staging image.
- `access_cb` — registered CPU-write watch on the guest range
  (`register_cpu_access_callback :877`).
- `initialized` — image contents valid (first bind uploads or clears).

**The invalid-surfaces pool**: `invalidate_surface()` (`:1044`) moves a
binding to `r->invalid_surfaces` rather than destroying it;
`get_any_compatible_invalid_surface` (`:1377`) recycles an image for a new
binding when `vk_format`+extent+usage match exactly (`:1367`);
`prune_invalid_surfaces` (`:1390`) caps the pool. So "invalidated" ≠
"destroyed": images are recycled to dodge reallocation.

`r->surface_generation` — bumped in `invalidate_surface` (`:1094`); the
staleness fence for zero-copy borrowed texture nodes (B2).

## Invariants

- **I1 — Residency uniqueness (default mode).** With alias mode OFF, at most
  one resident surface covers any guest byte: `surface_put` asserts no
  exact-base duplicate (`:1177`) and `invalidate_overlapping_surfaces` evicts
  every overlap before insert (`:1129`). [VERIFIED — assert in tree + SB
  byte-identical gate]
- **I2 — Alias-mode relaxation.** With `XEMU_ALIAS_SURFACES`, a resident
  surface may be overlapped (even same-base) by a *smaller* one only when
  `new->size × 4 ≤ old->size` and the old has no `upload_pending`
  (`:1150-1160`, `:2117-2128`). Everything else still evicts. [VERIFIED —
  ALIAS_KEPT counter + the LoC med-50 ms result]
- **I3 — Alias freshness arbitration.** When ranges overlap, the *newest*
  content for a range wins at consumption time via `draw_time` comparison in
  the surface-as-texture paths (B2); the kept parent serves **pre-clobber
  content** for its uncovered range — a deliberate approximation (real
  hardware would show clobbered garbage; LoC does not meaningfully sample it)
  (`:1129` comment). [VERIFIED functionally on LoC; approximation documented,
  not proven harmless for other titles — treat as per-title risk]
- **I4 — Newest-at-base ordering.** Insertion is always at the list head
  (`:1183`), so `pgraph_vk_surface_get` returns the newest binding at a base;
  alias children shadow parents on exact-base lookup, and the **look-deeper
  scans** recover a compatible same-base parent when the newest is
  incompatible (surface target switch `:2020-2031`; texture bind
  `texture.c:2186-2192`). [VERIFIED — LoC shadow-map cascade gone]
- **I5 — Size arithmetic.** `size = height × MAX(pitch, width×bpp)`; all
  overlap checks (`check_surface_overlaps_range :126`) use `[vram_addr,
  vram_addr+size)`. [VERIFIED — read directly]
- **I6 — Dirty write-back.** A surface with `draw_dirty` must be downloaded
  before its guest range is read by the CPU (memcb `surface_access_callback
  :776`), before eviction (`invalidate_overlapping_surfaces`,
  `expire_old_surfaces`, incompatible target-switch — all call
  `download_if_dirty` first), and at flip when `download_pending`. [DERIVED —
  traced at all call sites; no differential test exists]
- **I7 — Compatibility for reuse.** A found binding is reused as the render
  target iff `color`, `vk_format`, and `pitch` match, and (non-strict) its
  extent is a superset (`check_surface_compatibility :1420`). Zeta reuse
  additionally requires matching the color binding's extent (`:2105-2108`);
  color reuse must not make color/zeta ranges collide (`:2094-2103`).
  [DERIVED]
- **I8 — Age expiry.** A surface unused for `max_surface_frame_time_delta = 5`
  flips is downloaded-if-dirty and invalidated (`:34`, `:1405`). Consequence:
  any cross-frame surface reuse scheme (alias parents, zero-copy borrows) must
  tolerate 5-frame expiry. [VERIFIED — constant + code]
- **I9 — Fence-before-touch.** Evicting/overwriting an image must wait for
  both `last_write_submit` and `last_use_submit` (narrow-fence contract,
  `renderer.h:161`). Zero-copy sampling made *reads* fenceable too — a
  pre-2026-07 assumption ("only writes matter") no longer holds. [DERIVED —
  traced; violated nowhere we know, but no assert exists → candidate for one]
- **I10 — Layout single door.** Every transition of `image` goes through
  `pgraph_vk_surface_transition`; resting layout is
  `(COLOR|DEPTH_STENCIL)_ATTACHMENT_OPTIMAL`, except `feedback_mode` surfaces
  rest in `GENERAL`, and a zero-copy bind may leave `SHADER_READ_ONLY_OPTIMAL`
  which `begin_render_pass` restores for bound targets (`renderer.h:179-184`
  comment; full contract in B5). [VERIFIED — the missing-pair abort class
  during v8 bring-up proves the door is real]
- **I11 — Off-state parity.** The LoC-campaign additions (alias, feedback,
  zero-copy hooks) are inert when their flags are off; the flags-off path is
  byte-identical to pre-campaign behavior. [VERIFIED — SB regression gate,
  default flags, byte-identical log/counter comparison; visual parity checked
  by owner] ⚠ Note the SB-era features hosted here do NOT all default off:
  predictive readback defaults **ON** (`g_predl_enabled = true`,
  `surface.c:928`; killed by `XEMU_NO_PREDICTIVE_READBACK` or the
  `/tmp/predl-off` watch file) and narrow-fence defaults OFF but arms via a
  watch file (`/tmp/narrowfence-on` present ⇒ ON, `surface.c:958`). Full
  contracts in A2.

## Policies & decision points

**Target switch** (`update_surface_part :1988` → `:2000-2140`): on
surface-state change, build the prospective `target` binding
(`populate_surface_binding_target :1962`), then:
1. exact-base lookup → (alias mode) look-deeper if newest is incompatible;
2. found + compatible (I7) → reuse: adopt dims, `upload_pending |= mem_dirty`;
3. found + incompatible + alias-keep test (I2) → keep parent resident, fall
   through to create the small target alongside;
4. found + incompatible otherwise → download-if-dirty, invalidate ("evict
   reason: incompatible");
5. create: recycle from invalid pool if exact image match (`:1377`), else
   `create_surface_image :1256`; then `surface_put :1171` (which runs
   `invalidate_overlapping_surfaces` — the second alias-keep site — and
   registers the memcb).

**Why alias-keep exists:** LoC's shadow pipeline creates small transient
color blocks *inside* the 6.5 MB shadow-map zeta's footprint (including at its
exact base). Historic behavior evicted the parent (multi-MB download) and
re-uploaded next bind, every cascade, ~600×/frame worst case. Alias mode keeps
the parent; the 4× ratio keeps it conservative; `!upload_pending` guarantees
we never serve a parent whose content is known-stale relative to guest RAM.

**Downloads** happen at: CPU read of a watched range (memcb, immediate,
`:776`); flip (`process_pending_downloads :698`); eviction of dirty surfaces
(all sites); explicit flush. Predictive readback (A2) may add an eager
unbind-time download (`predictive_readback_on_unbind :991`).

**Uploads** happen lazily at bind time when `upload_pending` (guest wrote the
range since last GPU use; memcb sets it via dirty tracking).

**Scaling**: `set_surface_scale_factor` flushes everything and rebuilds at the
new scale; `image_scratch` stages the guest-pixel↔scaled blits.

## Failure modes

- **Eviction cascade** (pre-alias LoC): overlapping creates ping-pong evicting
  a large parent → multi-MB download+upload per cycle → `SURF_CREATE` and
  download counters explode, fps collapses. Detection: `SURF_CREATE`/frame ≫
  distinct targets/frame; fix: alias mode. [VERIFIED — the LoC round-2 story]
- **Alias staleness**: a title that *does* meaningfully read a clobbered
  parent range would see pre-clobber content (I3's approximation). Unproven
  harmless beyond LoC — that's why the flag is per-title opt-in. [ASSUMED
  risk, flagged]
- **Missing-transition abort**: any new layout path not in image.c's pair
  table aborts at runtime (B5); first v8 boot died exactly there. Armed by
  design.
- **Expiry vs borrow**: a zero-copy borrowed node whose surface expires (I8)
  must notice via `surface_generation`/`borrow_gen` or it samples a destroyed
  image — the guard is in B2's ensure path. [DERIVED — no crash ever observed,
  but the interaction is subtle; candidate for a targeted test]
- **Same color+zeta base** is explicitly unimplemented
  (`NV2A_UNIMPLEMENTED`, `:2037`): the other binding is force-unbound. A title
  that genuinely renders color and zeta to one base will misrender. [DERIVED]

## Instrumentation

- Counters (A3): `NV2A_PROF_SURF_CREATE`, `SURF_DOWNLOAD`, `SURF_UPLOAD`,
  `SURF_TO_TEX`, `ALIAS_KEPT` (both keep sites), plus the S2T/ZC families
  (B2).
- `XEMU_SURF_DUMP` / `XEMU_SURF_ACCESS_LOG` / `XEMU_SURF_ATTR` — geometry and
  access-path logging (A3 documents formats).
- Trace events: `nv2a_pgraph_surface_evict_reason("old"|"incompatible"|…)`,
  `surface_evict_overlapping`, `surface_target`, `surface_match_*`.
- Proposed (docs/perf/tool-specs.md T10): sentinel-triggered resident-surface
  population dump — the alias model was debugged blind without it.

## Verified-by

- SB regression gate: default-flag runs byte-identical across the campaign
  merges (rig scripts `sb-gates-v9*.sh`, `sb-regression-check*.sh`).
- LoC A/B matrix: `loc-bench-{off,ext,alias,zc,fb}.sh` + monitor-loadvm of the
  benchmark snapshot; med mspf 50.0 with alias ON vs eviction-cascade collapse
  OFF (docs/perf/loc/00-baseline.md).
- Counter attribution: `ALIAS_KEPT` nonzero exactly when the flag is on and
  the cascade scene plays; `SURF_CREATE` collapse across the A/B.
- In-tree asserts: base-uniqueness (`:1177`), DMA sanity (`:1926-1934`).

## Confidence & unknowns

Open [ASSUMED]/risk items, ranked:
1. Alias pre-clobber approximation harmless on titles other than LoC —
   per-title opt-in until proven (I3).
2. I9 has no enforcing assert; a future code path touching an image without
   waiting both submits would be silent until a visual glitch.
3. The `#if 0` swizzle-migrate path (`:2075-2092`) is dead upstream logic we
   inherited — unknown whether re-enabling would help any title; leave dead.
4. Interaction of surface scaling (scale > 1) with alias mode and with the
   S2T levers is untested — LoC and SB both measured at scale 1.
5. `check_invalid_surface_is_compatibile` ignores dimensions' guest meaning
   (recycles on exact extent match only) — believed safe because content is
   always re-initialized, but "believed" is the operative word.
