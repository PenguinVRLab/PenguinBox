# Spec — VK image-layout model (tracked layouts, rest states, feedback GENERAL)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| Tier | B — but the tracked-layout + feedback machinery is fork-original (v8/v9, 2026-07) |
| Code | `hw/xbox/nv2a/pgraph/vk/image.c` (269 lines), `surface.c:143-198` (transition/rest/flush helpers), `draw.c` (renderpass variants), `renderer.h` (`RenderPassState`, `image_layout`, `descriptor_layout`) |
| Status | v1 draft |
| Last verified | 2026-07-27 (v8 bring-up + v9 feedback; tags per claim) |

## Purpose & scope

Vulkan requires every image access to occur in a compatible `VkImageLayout`,
with transitions expressed as explicit barriers. Upstream xemu handled a small
fixed choreography; the 2026-07 work added **tracked layouts** (each surface
knows where its image *is*), **rest-state discipline** (where every image
returns to between uses), and the **feedback GENERAL regime** (surfaces
sampled while bound as render targets live permanently in
`VK_IMAGE_LAYOUT_GENERAL`). This spec is the single contract for "what layout
is any image in, and who may change it." Texture-side descriptor agreement is
B2/I8; renderpass mechanics beyond layout variants are B4.

## External interface

None directly — no env flag governs layouts. The regime is activated
indirectly by `XEMU_SURF2TEX_FEEDBACK` (B2) marking surfaces
`feedback_mode = true`. Internal API:

- `pgraph_vk_transition_image_layout(pg, cmd, image, format, old, new)`
  (`image.c:35`) — **the only legal way** to transition any pgraph-owned
  image. Emits the barrier with per-pair src/dst access masks and stages.
- `pgraph_vk_surface_transition(pg, cmd, surface, new_layout)`
  (`surface.c:143`) — surface wrapper: transitions AND updates the tracked
  `surface->image_layout`. Surfaces must use this, not the raw call.
- `pgraph_vk_surface_rest_layout(surface)` (`surface.c:155`) — returns the
  surface's home layout (see I2).
- `pgraph_vk_surface_feedback_flush(pg, cmd, surface)` (`surface.c:171`) —
  the GENERAL→GENERAL visibility barrier for feedback sampling, color/depth
  aware, deduped per redraw via `flushed_draw_time` (B2/P4).

## State & data structures

- `SurfaceBinding.image_layout` (`renderer.h:179-184`) — tracked actual
  layout. The comment IS the contract: *"Resting state is
  (COLOR|DEPTH_STENCIL)_ATTACHMENT_OPTIMAL; the only code that may LEAVE a
  different resting layout is the zero-copy texture bind
  (SHADER_READ_ONLY_OPTIMAL) — begin_render_pass restores bound targets. All
  transitions go through pgraph_vk_surface_transition."*
- `SurfaceBinding.feedback_mode` — sticky; flips the rest layout to GENERAL.
- `TextureBinding.descriptor_layout` (`renderer.h:285-287`) — what the
  descriptor set declares; must equal the image's draw-time layout (B2/I8).
- `RenderPassState.color_general` / `zeta_general` (`renderer.h`) — the
  renderpass-variant discriminators: a pass whose color (or zeta) attachment
  is a feedback surface uses `GENERAL` for that attachment's
  initial/final/subpass layouts instead of ATTACHMENT_OPTIMAL
  (`draw.c` `init_render_pass_state` / `create_render_pass`).
- `SurfaceBinding.image_scratch_current_layout` — same discipline for the
  scaling scratch image.

## Invariants

- **I1 — Single door.** Every layout transition of a pgraph image goes
  through `pgraph_vk_transition_image_layout`; every *surface* transition
  goes through the tracking wrapper. An unknown (old,new) pair hits the
  terminal `else` → hard abort (`image.c:263`). [VERIFIED — the v8 first-boot
  abort was this trap firing exactly as designed]
- **I2 — Rest states.** Between uses: color surfaces rest
  `COLOR_ATTACHMENT_OPTIMAL`; zeta surfaces rest
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`; **feedback surfaces rest `GENERAL`**
  (`pgraph_vk_surface_rest_layout`, `surface.c:155-169`). Owned texture
  images rest `SHADER_READ_ONLY_OPTIMAL`. [VERIFIED — v9 soak; violations
  would abort at the next transition or trip validation (when armed)]
- **I3 — Transient departures return.** A zero-copy bind may leave a
  non-feedback surface in `SHADER_READ_ONLY_OPTIMAL`; `begin_render_pass`
  restores bound targets to their rest layout before rendering
  (`draw.c` rest-restore). No other code may leave a surface off-rest.
  [VERIFIED — v8/v9 field behavior]
- **I4 — Feedback is sticky and layout-total.** Once `feedback_mode` is set
  (B2/P3.5), the surface rests GENERAL **for its remaining lifetime**, all
  its renderpasses use the `*_general` variant, and migration to a new image
  carries the tracked layout while resetting `feedback_mode = false`
  (`migrate_surface_image`, `surface.c:1329` — a migrated image is a new
  image; the pattern re-detects). [VERIFIED — cockpit + smoke scenes stable]
- **I5 — GENERAL sampling requires an explicit visibility barrier.** Writes
  to a GENERAL image are not automatically visible to sampling within the
  frame; `pgraph_vk_surface_feedback_flush` (GENERAL→GENERAL barrier) must
  run after the last write and before sampling, once per redraw. [VERIFIED —
  omitting it produced stale-feedback artifacts in v9 bring-up; per-bind
  running doubled renderpasses]
- **I6 — Renderpass compatibility across variants.** Vulkan renderpass
  compatibility ignores attachment layouts, so objects created against one
  variant are *legal* with the others. How the implementation actually uses
  this (per B4/I8): **framebuffers** are not keyed by variant and rely on
  compatibility (`draw.c:541+` uses the current `r->render_pass`);
  **pipelines** get separate cache entries per variant (the pipeline key
  includes `render_pass_state`, whose `color_general`/`zeta_general` fields
  discriminate) — duplication accepted for simplicity, compatibility not
  required there. [VERIFIED — v8/v9 field-proven; Vulkan spec
  §render-pass-compatibility]
- **I7 — Descriptor agreement.** `descriptor_layout` (B2) must equal the
  image's actual draw-time layout: `GENERAL` for feedback borrows,
  `SHADER_READ_ONLY_OPTIMAL` otherwise. Fallback when unset:
  `SHADER_READ_ONLY_OPTIMAL` (`shaders.c` descriptor write). [VERIFIED — v9]

## Policies & decision points

**The pair table** (`image.c:67-261`): 19 explicit (old,new) pairs, each with
minimal correct access masks/stages, then the GENERAL catch-all, then abort.
Upstream's original set covered UNDEFINED/TRANSFER/ATTACHMENT/SHADER_READ
flows; the 2026-07 additions (needed by S2T copies, zero-copy rebinds, and
depth-as-texture): `SHADER_READ→TRANSFER_SRC` (`:208`),
`DEPTH_ATTACH→SHADER_READ` (`:216`), `SHADER_READ→DEPTH_ATTACH` (`:225`),
`TRANSFER_SRC→SHADER_READ` (`:235`), `UNDEFINED→SHADER_READ` (`:243`).

**The GENERAL catch-all** (`image.c:250-261`): any pair touching GENERAL uses
conservative masks — `MEMORY_READ|MEMORY_WRITE` at `ALL_COMMANDS` stage
(src masks empty when old==UNDEFINED). Rationale: feedback surfaces reach
every other state; enumerating each pair invites the abort trap on rare
paths, and feedback surfaces are few (cost acceptance is deliberate).
**Policy: keep specific pairs tight; keep GENERAL conservative.** [VERIFIED
mechanism / [ASSUMED] that the conservatism is not a measurable cost — never
isolated]

**Adding a new path** (the process rule): if a new copy/bind flow introduces
a layout pair, add the explicit pair with minimal masks — do NOT widen the
catch-all. The abort is the detector; leave it armed.

## Failure modes

- **Unknown pair → abort** at first use (I1). Loud by design; the fix is a
  new explicit pair. [VERIFIED — v8 boot]
- **Tracked-layout drift** — if any code transitions an image without the
  wrapper, `image_layout` lies and a later "valid" transition uses wrong
  `oldLayout`: validation error (when armed — G1) or driver-dependent
  corruption. No in-tree detector today; grep discipline + review is the
  guard. [ASSUMED not currently violated — flagged for T1's validation
  revival]
- **Missed feedback flush** — stale sampling of the frame's earlier content
  (visual smearing), no numeric signal (I5).
- **Migration forgetting layout** — `migrate_surface_image` carries
  `image_layout`; a future migration path that forgets would desync tracking
  (same class as drift). [DERIVED]

## Instrumentation

- `ZC_FLUSHES` counter — feedback barriers issued (should track redraw count,
  not bind count; ratio ≫1 signals the per-bind regression returning).
- Validation layers, when working (G1/T1), are THE detector for this whole
  spec — every invariant here maps to a VUID class. Until then: the abort
  trap (I1) and visual review.
- `XEMU_SURF2TEX_DEBUG` dumps include the chosen path, implying layout
  regime.

## Verified-by

- v8/v9 bring-up sequence (docs/perf/loc/00-baseline.md §v8, §v9): every
  invariant above was violated at least once during development and produced
  its documented failure signature — the spec is written from those scars.
- LoC soak: cockpit + smoke scenes (feedback-heavy) stable over full missions.
- SB gates: default path (no feedback surfaces ever) byte-identical.

## Confidence & unknowns

1. [ASSUMED] GENERAL catch-all conservatism is performance-negligible (few
   feedback surfaces, ~1 flush/redraw). Never isolated; measure if a
   feedback-heavy title underperforms expectations.
2. [ASSUMED] No remaining unwrapped transition sites (I1 audit was manual,
   2026-07; new code is the risk — spec-with-change rule applies).
3. [DERIVED] Scratch-image layout tracking (`image_scratch_current_layout`)
   follows the same discipline but was only exercised at scale 1 — scaled
   feedback surfaces untested (B1 unknown #4 overlaps).
4. Validation layers have never checked this machinery (G1) — every
   [VERIFIED] above is field-verified, not validation-verified. Re-tag after
   T1 lands and a clean validated run exists.
