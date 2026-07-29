# Spec — VK texture cache & surface-as-texture (S2T)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| Tier | B (inherited xemu, heavily modified by us) |
| Code | `hw/xbox/nv2a/pgraph/vk/texture.c` (~3,000 lines), structs `vk/renderer.h:245-288`, compute helper `vk/surface-compute.c` |
| Status | v1 draft |
| Last verified | 2026-07-27 (LoC campaign v1–v9.4; tags per claim) |

## Purpose & scope

The texture cache turns **guest texture state** (the per-stage NV2A texture
registers + the guest memory they point at) into bindable host
`VkImage`/`VkImageView`/`VkSampler` triples. Its hard problem — and the site
of the entire LoC campaign — is **surface-as-texture (S2T)**: guest textures
whose backing memory is (or overlaps) a live render-target surface, where
guest RAM does not hold the truth (the GPU image does). This spec owns the
cache keying/lifecycle and the S2T path-selection contract. Surface residency
is B1; layout choreography is B5; format semantics are B3.

## External interface

**Env flags** (getenv-once; each announces on stderr when ON; every `NO_`
kill-switch wins over its enabler; all default OFF with byte-identical OFF
paths [VERIFIED — SB default-gate byte-identical runs]):
- `XEMU_SURF2TEX_EXT` (`texture.c:1293`) — extended reinterpretation binds:
  gather plans, swizzled-quadrant copy, linear→swizzled bounce.
- `XEMU_SURF2TEX_ZEROCOPY` (`:1320`) — exact-match color S2T binds sample the
  surface image directly (borrowed nodes, no per-rebind copy).
- `XEMU_SURF2TEX_FEEDBACK` (`:1344`) — **requires ZEROCOPY** (ignored with a
  stderr note otherwise): allows zero-copy sampling of the *currently bound*
  render target via GENERAL layout (the Xbox feedback pattern).
- `XEMU_SURF2TEX_DEBUG` — rate-limited geometry dumps (A3).

**In-tree API:** `pgraph_vk_bind_textures` (`:2786`) per draw;
`create_texture` (`:2089`) on cache miss; `pgraph_vk_trim_texture_cache`
(`:2942`). Consumes B1's `pgraph_vk_surface_get*` lookups and the
`r->surfaces` list directly (deeper scan `:2186`).

## State & data structures

`TextureKey` (`renderer.h:245`): `TextureShape state` (dims, format, levels,
cubemap…), `texture_vram_offset/length`, `palette_vram_offset/length`,
`scale`, `filter`, `address`, `border_color`, `max_anisotropy`. The key is
hashed (`fast_hash`) for LRU lookup (`:2397`). **The key contains no content
hash** — content freshness is handled separately (below).

`TextureBinding` (`renderer.h:258`):
- `node` — LRU node; cache is an `Lru` with evict callbacks
  (`texture_cache_entry_pre/post_evict :2883/:2906`).
- `possibly_dirty` + `hash` — content-freshness machinery for RAM-backed
  textures: when guest memory *might* have changed, the data is rehashed and
  compared before reuse. The GL backend shares this contract (B9).
- `draw_time`, `submit_time` — freshness/fence clocks (B1's clock rule).
- `copied_frame_time` (v9.4) — `pg->frame_time` of the last depth-as-texture
  copy into this node; **the per-frame dedup key** (see P5). `-1` on fresh
  nodes (`:2540`).
- `borrowed`, `borrow_gen`, `borrow_image`, `borrow_surface` (zero-copy):
  this node's `image_view` borrows a surface's `VkImage`; `image`/`allocation`
  are `VK_NULL_HANDLE` and must not be destroyed
  (`texture_cache_release_node_resources :2855` guards this).
  `borrow_surface` is valid **only while** `borrow_gen ==
  r->surface_generation` (`renderer.h:282`).
- `descriptor_layout` — the image layout this binding's descriptor declares:
  `SHADER_READ_ONLY_OPTIMAL` for owned nodes and plain borrows, `GENERAL` for
  feedback borrows (`renderer.h:285`; consumed by shaders.c when writing
  descriptor sets).

## Invariants

- **I1 — Truth source.** For a texture whose range is covered by a resident,
  GPU-fresh surface, the surface image is the truth; guest RAM is stale until
  download. Binding must therefore either copy from the surface image (paths
  1–4) or sample it directly (path 5) — never trust RAM. [VERIFIED — the
  entire campaign is this invariant's evidence]
- **I2 — Borrowed nodes own nothing.** A borrowed binding holds no image
  memory; release/evict must destroy only the view and sampler
  (`:2855`). Violation = double-free or leak. [VERIFIED — in-tree guard +
  no leaks across soak]
- **I3 — Borrow staleness.** A borrowed node is valid only while (a) no
  surface invalidation occurred since the borrow (`borrow_gen ==
  r->surface_generation`), (b) the surface's `VkImage` is unchanged
  (`borrow_image == surface->image`), and (c) the bind is still zero-copy
  eligible. Any failure rebuilds the node (`:2400-2412`). The generation also
  bumps on **feedback promotion** (`:2391`) — existing borrows' descriptor
  layouts go stale when a surface flips to GENERAL rest. [VERIFIED — shadow
  A/B + soak; the promotion fence exists because stale descriptors were
  observed]
- **I4 — Owned→borrowed migration.** A warm cache hit that is now zero-copy
  eligible must migrate (release + rebuild borrowed), else warm nodes copy
  forever (`:2408` comment). [VERIFIED — the SURF_TO_TEX collapse came only
  after this migration existed]
- **I5 — Depth is never borrowed.** Zeta surfaces always use the converting
  copy path (`ZC_REJ_ZETA_SURF :2361`), because Xbox depth formats are decode
  *chains*: the copy converts raw bits and the shader decodes; sampling a
  depth image view directly makes the sampler pre-normalize → double decode →
  corrupted shadow comparisons. [VERIFIED — the v9.3 shadow regression, its
  v9.4 revert, and owner visual confirmation]
- **I6 — Per-frame depth dedup.** Depth-as-texture copies dedup on
  `copied_frame_time != surface->frame_time` (`:2487`), i.e. **once per
  flip**, because the consumer (soft-particle smoke) samples finished opaque
  depth whose content is final before transparents draw. Keying on
  `draw_time` re-copied ~700×/frame for the same content. [VERIFIED — LoC
  200 ms-class frames eliminated; fps 4→20]
- **I7 — Dedup keys match consumer cadence** (the generalized I6): any cache
  suppressing repeated work must key on the clock of the *consumer's*
  freshness requirement (per-draw `draw_time` vs per-flip `frame_time`), not
  the producer's. [VERIFIED on I6; standing design rule for future paths]
- **I8 — Descriptor/layout agreement.** At draw time, each bound texture's
  descriptor `imageLayout` equals the actual layout its image will hold during
  the pass: owned/plain = `SHADER_READ_ONLY_OPTIMAL`, feedback borrow =
  `GENERAL` (`descriptor_layout`, bind-time ensure loop in
  `pgraph_vk_bind_textures`). [VERIFIED — v9 bring-up failures until this
  held; see B5]
- **I9 — Off-state parity.** All flags OFF ⇒ byte-identical behavior to
  pre-campaign xemu. [VERIFIED — SB gates every round]

## Policies & decision points

**P1 — Cache lookup.** Key built from texture state; `lru_lookup` by hash+
compare (`:2397-2399`). `binding_found` means a usable image or a live
borrow (`:2399`), then the staleness/migration gate (I3/I4) may force a
rebuild. On hit, `possibly_dirty` propagates and may force rehash of
RAM-backed content.

**P2 — S2T detection.** At create/bind, the texture range is checked against
resident surfaces (exact base first, then the alias look-deeper scan
`:2186-2192`, then overlap scans). `surface_to_texture_compat_reason`
(`:1283`) classifies the relationship; incompatibilities land in the
`S2T_MISS_*` counters — the campaign's diagnostic backbone.

**P3 — Path selection** (first eligible wins):
1. **Direct copy** (`copy_surface_to_texture :1161` / zeta variant `:953`) —
   shapes/formats compatible: one `vkCmdCopyImage` (color) or the
   depth-convert pipeline (zeta).
2. **EXT gather plan** (`copy_gather_plan_to_texture :1580`) — the texture is
   assembled from N surface sub-rects (row-compatible reinterpretations,
   offsets, tiles). Counters `S2T_EXT_COPY/REGIONS/PARTIAL`; rejects →
   `S2T_REJ_*`.
3. **EXT swizzled quadrant** (`copy_swizzled_quadrant_to_texture :1716`) —
   swizzled texture over swizzled surface at the same base: the first 4^m
   Morton texels of a 2^n square are exactly its top-left 2^m quadrant, so a
   plain `vkCmdCopyImage` of the quadrant is correct (B3 for the math).
   Counter `S2T_EXT_QUAD`.
4. **EXT bounce** (`bounce_linear_surface_to_swizzled_texture :1767`) —
   swizzled texture over *linear* surface: `vkCmdCopyImageToBuffer` at
   surface pitch (buffer = guest byte stream; gaps zero-filled) → compute
   deswizzle (`surface-compute.c`, `out[gid] = in[bias + morton(x,y)]`, word
   bias for sub-word offsets) → `vkCmdCopyBufferToImage`. Counter
   `S2T_EXT_BOUNCE`.
5. **ZEROCOPY borrow** (`:2365-2395`) — eligibility: color surface, exact
   `vk_format` match (`ZC_REJ_FMT`), `levels==1 && !cubemap &&
   dimensionality==2` (`ZC_REJ_SHAPE`), not the bound zeta (checked at
   `:2374`; its counter `ZC_REJ_ZETA_BOUND` is **vestigial** — declared in
   debug.h but with no increment site since the v9.4 revert, permanently 0
   — A3/I-11), never a zeta surface (`ZC_REJ_ZETA_SURF`, I5), and
   if it IS the bound color target, FEEDBACK must be on. First feedback bind
   sticky-marks the surface (`feedback_mode = true`, generation bump,
   `:2386-2392`) — from then on it rests GENERAL and uses the
   `color_general` pass variant (B5).
Fallback when nothing matches and EXT can't help: the historical
**download+rehash** path (surface → guest RAM → re-upload as texture) — the
storm that produced LoC's 4 fps [VERIFIED — S2T_MISS counters + A/B].

**P4 — Feedback flush cadence.** Feedback-sampled surfaces need a
GENERAL→GENERAL visibility barrier so writes are visible to sampling; it runs
**once per redraw** keyed on `flushed_draw_time` (B1 field) — per-bind
flushing doubled render-pass count [VERIFIED — measured ~700→1500 rpass].

**P5 — Depth-as-texture copy dedup** (`:2479-2495`, warm path `:2754`): when
FEEDBACK is on and the texture is a zeta surface, copy once per
`surface->frame_time`, stamping `copied_frame_time` and `draw_time`.

## Failure modes

- **Fallback storm** — S2T incompatibility class with EXT off (or an
  unhandled shape): per-draw multi-MB download+upload; symptom: fps collapse
  with `S2T_MISS_*` and download counters pegged. [VERIFIED — LoC baseline]
- **Double decode** — any future path that lets a sampler see raw Xbox depth
  bits through a normalizing view corrupts depth-compare visuals *without any
  numeric error signal* — counters and frame times stay perfect; only pixels
  are wrong. Detection is visual (T4 golden-frame gate, specced). [VERIFIED —
  v9.3]
- **Stale borrow** — a missed generation bump (new invalidation site added
  without it) would leave borrows sampling freed/migrated images:
  crash or garbage. Mitigation: I3's three-condition gate; risk lives in
  *future* B1 edits — the spec-with-change rule exists for exactly this.
- **Descriptor/layout mismatch** — validation-layer error class (when
  validation works — G1) and undefined behavior on some drivers; prevented by
  I8's single source (`descriptor_layout`).
- **LRU eviction of a borrow mid-frame** — pre_evict refuses nodes in flight
  (`:2883`) [DERIVED — traced, not deliberately exercised].

## Instrumentation

- Counters (all in `hw/xbox/nv2a/debug.h`, visible in the Video Debug
  overlay and script dumps — A3): `SURF_TO_TEX` (every S2T bind),
  `S2T_MISS_*` (compat-reason classes, always on), `S2T_EXT_{COPY,QUAD,
  BOUNCE,REGIONS,PARTIAL}`, `S2T_REJ_*` (EXT rejections),
  `ZC_REJ_{FMT,ZETA_SURF,SHAPE}`, `ZC_FLUSHES` (feedback barriers).
  (`ZC_REJ_ZETA_BOUND` is declared but dead — no increment site since v9.4;
  always reads 0.)
- `XEMU_SURF2TEX_DEBUG=1` — rate-limited dumps of the actual tex/surface
  geometry at decision points (the tool that named every LoC wall).
- Each flag prints an arming banner on stderr — scripts must grep the banner
  before trusting flag-dependent numbers (armed-detector rule).

## Verified-by

- LoC A/B matrix (`loc-bench-{off,ext,zc,fb}.sh`, monitor-loadvm benchmark
  snapshot): EXT 4→16 fps; ZEROCOPY+FEEDBACK eliminated the 200 ms class;
  final steady ~20 fps (docs/perf/loc/00-baseline.md).
- v9.3→v9.4 shadow A/B (`shadow-ab.sh`) + owner visual sign-off ("it all
  looks good").
- SB regression gates every round: default OFF byte-identical; flags-on
  "engaged-and-benign" (counters move, output subjectively unchanged — visual
  gate still owner-eyes only).
- Counter baselines: SB `SURF_TO_TEX` 16,626 ±2%, fallback 99 ±5 per bench
  window (T11 baseline data).

## Confidence & unknowns

1. [ASSUMED] Zero-copy eligibility is deliberately narrow (2D, single-level,
   non-cube, exact format). Whether widening (e.g. mipped RTT) is safe is
   unknown — widen only with the visual gate (T4) in place.
2. [ASSUMED] The `possibly_dirty`/hash contract fully matches the GL backend
   after the parity fix — no cross-backend differential test exists (B9).
3. [DERIVED] LRU trim policy (`pgraph_vk_trim_texture_cache`) sizing under
   borrow-heavy workloads — borrows are cheap but hold `surface_generation`
   coupling; long-session behavior unmeasured.
4. [ASSUMED] The bounce path's zero-fill for pitch gaps matches real-hardware
   garbage semantics well enough for every title (LoC yes; untested
   elsewhere).
5. Palette (indexed-color) textures over surfaces: never exercised in either
   campaign; the interaction of palette keys with S2T paths is [ASSUMED]
   correct-by-fallback (they should fail compat → historical path).
