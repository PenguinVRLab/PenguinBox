# Spec — Format model (kelvin formats, decode chains, swizzle layout)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| Tier | B (inherited xemu; the decode-chain doctrine is campaign-earned) |
| Code | `hw/xbox/nv2a/pgraph/texture.{c,h}` (kelvin info maps, CPU converters), `pgraph/swizzle.c`, `pgraph/vk/constants.h` (VK maps), `vk/surface.c:2311-2350` (surface zeta map init), `vk/surface-compute.c` (GPU deswizzle) |
| Status | v1 draft |
| Last verified | 2026-07-27 (the v9.3/v9.4 shadow incident is this spec's founding evidence) |

## Purpose & scope

Three format systems must agree for every texel the guest sees:
1. **Kelvin texture formats** — `NV097_SET_TEXTURE_FORMAT_COLOR_*` (66-entry
   space): what the guest declares a texture to be.
2. **Kelvin surface formats** — `NV097_SET_SURFACE_FORMAT_{COLOR,ZETA}_*`:
   what the guest declares a render target to be.
3. **Host formats** — `VkFormat` (+ component swizzles on the view) chosen to
   represent each.

This spec owns the mapping tables, the **decode-chain doctrine** (the
campaign's most expensive lesson), and the **Morton/swizzle layout math**.
Which *path* moves data between surfaces and textures is B2; this spec
defines what makes such a move *meaning-preserving*.

## External interface

None at runtime — the maps are static data:
- `kelvin_color_format_info_map[66]` (`texture.h:53`,
  `BasicColorFormatInfo {bytes_per_pixel, linear, depth}` `texture.h:47-51`)
  — the renderer-independent facts per texture format. `linear` = pitch
  addressing (`LU_`/unswizzled); !linear = Morton (`SZ_`). `depth` = the
  format participates in the depth decode chain (see I3).
- `kelvin_color_format_vk_map[]` (`vk/constants.h`) — per texture format: the
  `VkFormat` + a `VkComponentMapping` view swizzle.
- `kelvin_surface_color_format_vk_map[]` / `r->kelvin_surface_zeta_vk_map[]`
  (`vk/surface.c:2324-2333`) — surface-side host formats; the zeta map is
  runtime-initialized because Z24S8's representation depends on device
  support (D24_UNORM_S8 vs D32_SFLOAT_S8 fallback).
- CPU converters: `pgraph_convert_texture_data` (`texture.h:55`) and the
  swizzle_box/unswizzle_box family (`swizzle.c:83,129`).
- GPU deswizzle: `pgraph_vk_dispatch_deswizzle_u32`
  (`surface-compute.c:358`, GLSL at `:333`).

## State & data structures

The map entries themselves (above). Key format facts used by the rest of the
system:

- **Bytes-per-pixel is not semantics.** Two formats with identical texel size
  and even identical bit patterns can require different *interpretation
  pipelines* (I3). Never reason "same bits, so interchangeable."
- **The component map is part of the format.** E.g.
  `LU_IMAGE_DEPTH_Y16_FLOAT` maps to `VK_FORMAT_R16_UNORM` with swizzle
  `(R,0,1,0)` while `Y16_FIXED` uses `(R,0,0,0)` (`vk/constants.h:285-292`) —
  the shader's decode contracts on those channels.
- **Surface zeta formats**: `Z16 → zeta_d16`, `Z24S8 → device-dependent`
  (`surface.c:2324-2333`). Surface color formats carry
  `host_bytes_per_pixel`; a zero entry aborts as unimplemented
  (`surface.c:1905-1910`).

## Invariants

- **I1 — A format is a decode PATH, not a bit pattern.** The meaning of texel
  bits is defined by the *entire chain* that touches them: storage bits →
  (optional CPU/GPU conversion) → view format + component swizzle → sampler
  normalization → shader decode arithmetic. Correctness = the chain composes
  to the Xbox's semantics EXACTLY ONCE. [VERIFIED — the v9.3 incident, below]
- **I2 — Each decode step happens exactly once.** The canonical violation:
  v9.3 sampled a D16 zeta surface directly through a depth-aspect view.
  Storage bits were identical to the copy path's Y16 texture — but a
  `D16_UNORM` view makes the SAMPLER pre-normalize to [0,1], and the fragment
  shader then applied the Xbox float-depth decode again. Double decode →
  wrong shadow comparisons → owner-visible bad pixels, with zero numeric
  error signal (`texture.c:2345-2356` carries the full postmortem as the
  v9.4 CORRECTNESS REVERT comment). [VERIFIED — reproduced, reverted,
  owner-confirmed fixed]
- **I3 — Depth-as-texture uses the converting copy.** `LU_IMAGE_DEPTH_Y16_*`
  (kelvin 0x30 FIXED / 0x31 FLOAT) textures over zeta surfaces take
  `copy_zeta_surface_to_texture` (B2 path 1): raw 16-bit bits are copied into
  an `R16_UNORM` texture and the shader applies the decode
  (FLOAT: the Xbox float-depth decode; FIXED: fixed-point normalize). Zeta
  surfaces are NEVER sampled through borrowed views (B2/I5). [VERIFIED]
- **I4 — Morton order (swizzled layout).** `generate_swizzle_masks`
  (`swizzle.c:48-71`) assigns address bits round-robin x,y,z from bit 0 while
  each dimension has bits left; masks are disjoint and cover the address
  space (in-tree assert `swizzle.c:66`). For square 2D (width==height):
  **x owns the even bits, y the odd bits** — interleave = `part1by1(x) |
  (part1by1(y)<<1)` (mirrored in the GPU deswizzle GLSL,
  `surface-compute.c:330-352`). [VERIFIED — CPU and GPU implementations agree
  in the field (bounce path renders correctly)]
- **I5 — Quadrant nesting.** In a 2^n × 2^n Morton square, the first 4^m
  texels are exactly the top-left 2^m × 2^m quadrant, itself in Morton order
  (follows from I4's bit interleave: indices < 4^m ⟺ x < 2^m ∧ y < 2^m).
  This is what makes B2's quadrant path a plain `vkCmdCopyImage`. [VERIFIED —
  the quadrant path produces correct output on LoC's shadow block]
- **I6 — Non-square swizzle degenerates gracefully.** When width ≠ height,
  the round-robin gives the larger dimension its extra bits at the top —
  sub-rectangles are then NOT contiguous Morton prefixes, so I5-style
  shortcuts are square-only. The bounce path (full-address recompute per
  texel) is the general case. [DERIVED — from the mask construction; the EXT
  code only takes the quadrant shortcut for square shapes]
- **I7 — Surface pitch gaps are zero-filled on export.** When the bounce path
  serializes a surface to the guest-byte-stream buffer at surface pitch, the
  gap bytes (pitch > width×bpp) are written as zeros, not garbage
  (B2 path 4). Real hardware would hold stale RAM there. [VERIFIED mechanism/
  [ASSUMED] the difference is never guest-visible — flagged]

## Policies & decision points

- **VK format choice policy**: prefer the exact-size VK format + view
  swizzle over CPU conversion; fall back to `pgraph_convert_texture_data`
  only where VK has no representation. Several entries carry `// FIXME`
  markers (e.g. both DEPTH_Y16 entries — `R16_UNORM` loses the FLOAT
  distinction at the view level, which is precisely why the shader decode
  step exists and why I2 matters).
- **Z24S8 device fallback** (`surface.c:2327-2333`): D24_UNORM_S8 when
  supported, else D32_SFLOAT_S8 — a *representation* change that the decode
  chain must absorb (depth precision differs; [ASSUMED] no title-visible
  effect — inherited upstream decision, never audited by us).
- **When a new S2T path is proposed** (the doctrine distilled): write out the
  full decode chain for the copy path and the proposed path side by side; if
  any step (sampler normalize, view swizzle, shader arithmetic) differs, the
  paths are NOT equivalent regardless of storage bits. This one-paragraph
  check would have caught v9.3 at design time.

## Failure modes

- **Double/missing decode** (I2 violated): visually wrong output, numerically
  silent. Only defenses: the design-time chain check (above) and the visual
  golden gate (T4, specced). [VERIFIED class]
- **Unimplemented surface format**: hard abort with format code on stderr
  (`surface.c:1905`) — loud by design.
- **Component-swizzle drift**: an edit to a vk_map entry's swizzle silently
  changes shader inputs for every title using that format — treat
  `vk/constants.h` edits as high-risk, title-wide changes.
- **Swizzle math divergence**: CPU (`swizzle.c`) and GPU
  (`surface-compute.c`) implement the same interleave independently; an edit
  to one without the other corrupts only the paths using the edited one —
  subtle, scene-dependent. The shared-constant assert (`swizzle.c:66`) covers
  masks, not the GLSL. [ASSUMED currently in sync — verified only by field
  output]

## Instrumentation

- No dedicated format instrumentation exists. Format errors surface as:
  aborts (unimplemented), `S2T_MISS_*`/`ZC_REJ_FMT` counters (B2 —
  format-mismatch rejections), or wrong pixels (T4 territory).
- `XEMU_SURF2TEX_DEBUG` dumps include both sides' formats at S2T decision
  points — the practical format debugger today.

## Verified-by

- v9.3→v9.4 shadow incident + revert + owner visual confirmation
  (docs/perf/loc/00-baseline.md corrections; `texture.c:2345` comment).
- Quadrant + bounce paths rendering correctly on LoC (S2T_EXT_QUAD/BOUNCE
  counters engaged, scenes visually correct per owner).
- In-tree asserts: swizzle mask disjointness (`swizzle.c:66`), surface format
  implemented (`surface.c:1905`).

## Confidence & unknowns

1. [ASSUMED] Zero-filled pitch gaps never guest-visible (I7) — a title that
   reads framebuffer gap bytes would see zeros instead of stale data.
2. [ASSUMED] Z24S8→D32S8 fallback precision difference is title-invisible
   (inherited upstream, unaudited).
3. [ASSUMED] CPU and GPU swizzle implementations stay in sync (no shared
   source of truth — candidate: generate the GLSL constant from swizzle.c's
   masks, or add a boot-time self-test comparing both on a test pattern).
4. [DERIVED] The FIXED vs FLOAT Y16 decode difference lives entirely in the
   shader; this spec has not enumerated the exact GLSL decode functions —
   v1.1 should cite them (psh.c generated code) so the chain is written down
   end to end.
5. The 66-entry texture format space has entries we have never seen a title
   use; their vk_map choices are upstream-inherited and untagged — audit
   opportunistically as titles exercise them.
