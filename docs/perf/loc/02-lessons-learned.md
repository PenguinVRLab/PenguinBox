# Lessons learned — the Line of Contact performance campaign

Deep notes from the 2026-07-27 campaign (~1–4 fps → steady ~20 fps, nine
builds, one visual regression caught and fixed). Everything here was paid for;
the point of this document is to not pay twice. Companion docs:
[`00-baseline.md`](00-baseline.md) (the chronological record),
[`01-zerocopy-design.md`](01-zerocopy-design.md) (the zero-copy design and its
honest verdict), [`HANDOFF-packaging.md`](HANDOFF-packaging.md) (packaging),
and [`../tooling-gaps.md`](../tooling-gaps.md) (what tooling failed us).

---

## Part 1 — Novel technical findings

### 1.1 The Xbox feedback-rendering pattern, and the Vulkan answer

The single most valuable finding of the campaign. Xbox games freely **sample a
render target while it is still bound for drawing** — unified memory makes
this free and natural. Line of Contact does it in two distinct forms:

- **Color feedback**: cockpit sub-monitors — draw a block, sample it, draw,
  sample, hundreds of times per frame.
- **Depth feedback**: soft-particle smoke/haze — every particle quad samples
  the *bound* D16 depth buffer to fade against geometry.

Vulkan forbids sampling an image bound as an attachment (outside special
mechanisms), so the emulator serviced every such bind with a full
copy/conversion — ~700 per frame, ~200 ms of pure overhead.

**The solution shape** (v9, `XEMU_SURF2TEX_FEEDBACK`):

1. Sticky per-surface `feedback_mode`; the surface **rests in
   `VK_IMAGE_LAYOUT_GENERAL`** (legal for attachment use, sampling, and
   transfer simultaneously — no per-cycle layout thrash).
2. **Render-pass variants** (`color_general`, `zeta_general` bits in
   `RenderPassState`) carry the attachment layouts.
3. The write→read hazard is a `GENERAL→GENERAL` image barrier
   (`COLOR_ATTACHMENT_WRITE→SHADER_READ` or the depth equivalent), emitted
   **once per redraw** — see §2.3 for why not per bind.
4. Sampling descriptors declare per-binding layouts (`descriptor_layout` on
   the texture node) instead of a hardcoded `SHADER_READ_ONLY_OPTIMAL`.

**The key spec fact that makes this cheap**: *render-pass compatibility
ignores layouts.* Two passes differing only in attachment layouts are
compatible, so **all existing pipelines and framebuffers work with the GENERAL
variant unchanged**. What looked like a pipeline-cache-doubling change
collapsed into "pick the right `VkRenderPass` handle at begin."

No self-dependencies were needed: the bind machinery already breaks the render
pass between the write-draw and the sampling draw (nondraw commands), so the
sequence is pass{write} → barrier → pass{sample} — plain and legal.

### 1.2 Depth formats are *decode paths*, not bit patterns

The one visual regression of the campaign, and the deepest correctness lesson.

The v9.3 shortcut sampled the D16 zeta directly through a depth-aspect view,
justified as "bit-identical to the Y16 texture the conversion produced." The
bits *are* identical — **the decode is not**. LoC binds its shadow map as
`LU_IMAGE_DEPTH_Y16_FLOAT` (kelvin 0x31, `.depth=true` in
`kelvin_color_format_info_map`). Two pipelines:

- **Conversion path (correct)**: raw 16-bit values copied into a Y16 texture →
  the *fragment shader* applies the Xbox float-depth decode.
- **Depth-view path (wrong)**: `D16_UNORM` sampling makes the *sampler*
  pre-normalize to [0,1] → the shader then decodes **again** → double decode →
  wrong depth fed to the shadow comparison → visibly broken shadow pixels.

**Rule**: any claim that a depth reinterpretation is "bit-identical" must
trace the *full* decode chain — storage bits → sampler transform → shader
transform. If the shader applies a decode, handing it pre-normalized values is
a semantic change even when storage is untouched.

Corollary that also bit us: `vk_format_texel_size(fmt) == 2` accepts
`R5G6B5`, `A1R5G5B5`, `A4R4G4B4`, `R8G8` — *size* equality is not *semantic*
equality. Gate reinterpretations on the actual format pair, never on texel
size alone.

### 1.3 Morton (Xbox swizzle) nesting — the quadrant property

For square power-of-two Morton layouts: **the first `4^m` texels of a
`2^n × 2^n` Morton square are exactly its top-left `2^m × 2^m` quadrant, in
that quadrant's own Morton order.** Consequence: a swizzled `64×64` texture
bound at the base address of a swizzled `128×128` surface is *precisely* the
surface's top-left 64×64 rect — and since xemu stores both images deswizzled,
a plain `vkCmdCopyImage` of that rect is exact. No compute needed. (Both dims
must be square pow2; the non-square Xbox swizzle interleaves the small
dimension then runs linear, and the nesting property breaks.)

The bit convention (from `pgraph/swizzle.c generate_swizzle_masks`): x takes
the even bits starting at bit 0, y the odd bits — standard Morton with
x-first. A GLSL `part1by1` interleave reproduces it exactly.

### 1.4 The buffer-bounce: memory-reinterpretation semantics on the GPU

Xbox games reinterpret memory: render a linear D16 depth map, then bind a
*swizzled A8R8G8B8 texture over the same bytes*. Byte-level reinterpretation
(the "storm" class: 596 binds/frame). The CPU fallback replicated it by
download → reinterpret → upload. The GPU-side equivalent that removed it:

```
vkCmdCopyImageToBuffer   surface image → scratch buffer, bufferRowLength =
                         surface pitch/bpp   (reproduces the MEMORY BYTE STREAM,
                         gap bytes pre-zeroed with vkCmdFillBuffer)
compute dispatch         out[gid] = in[bias + morton(x,y)]   (the texture's
                         addressing of those bytes; bias = word-aligned offset
                         into the surface for interior binds)
vkCmdCopyBufferToImage   scratch → texture image (texture's own layout)
```

The insight: **a buffer with the source's row stride *is* the guest memory
window**, so any destination interpretation (different pitch, Morton order,
different bpp grouping) is just addressing math in a shader. Reusable for any
"texture reads surface bytes under a different layout" case.

Practical notes: `BUFFER_COMPUTE_DST` is the transfer-*destination*-capable
buffer (use it as bounce **input**), `BUFFER_COMPUTE_SRC` the
transfer-source-capable one (bounce **output**) — the names describe transfer
roles, which are inverted from the compute dataflow. Guard cross-bounce reuse
with buffer barriers covering the *previous* bounce's compute-read and
transfer-read.

### 1.5 Surface aliasing: games reuse memory, the surface cache assumes they don't

LoC's shadow pipeline places transient color blocks **inside** — and even at
the **exact base address of** — the 6.5 MB shadow-map zeta's footprint. The
historical model (one surface owns a range; creation invalidates overlaps)
turned this into a mutual-eviction storm: every create/re-target evicted the
parent (6.4 MB download), every full-map bind re-uploaded it (~127 MB/window
of CPU traffic).

The alias model that fixed it (`XEMU_ALIAS_SURFACES`):

- **Keep large parents resident** under much-smaller children (4× size ratio,
  `!upload_pending`) — at *all three* eviction sites: create-overlap, the
  target-switch "incompatible" evict (the sneaky one — a separate code path
  that bypassed the first fix), and exact-base children.
- Exact-base coexistence works because surfaces insert at the list head:
  `surface_get` naturally returns the newest at a base. Add **look-deeper
  scans** (texture bind + target switch) so a shape-compatible parent behind a
  newer child is still found and reused.
- Readers arbitrate by **draw_time freshness**: an overlapping surface drawn
  no later than the host can't hold newer content for the range (the host's
  own draw covered it afterwards) — so stale parents are ignored, while a
  *fresher* overlapping sibling still forces the safe fallback.

Steady state on the benchmark: both aliases permanently resident, zero
evictions, downloads med 0.8 ms (was bursts to 173 ms).

Semantics note, stated honestly: aliased ranges serve *pre-clobber* content
(the parent's image, not the child's bytes). On real hardware the clobbered
rows would be garbage the game doesn't meaningfully sample. This is a
deliberate approximation and one more reason human eyes must review (it
passed).

### 1.6 Dedup keys must match what the effect consumes, not what changed

Two versions of the same lesson:

- **Depth-as-texture copies**: keyed on `surface->draw_time`, which advances
  with *every particle draw* → the entire shadow map re-converted ~700×/frame.
  But soft particles sample the **finished opaque scene's** depth — one
  snapshot per frame is what the effect *means*. Re-keying on
  `pg->frame_time` (once per frame) collapsed 700 conversions to 1–2 with
  identical output. (`frame_time` increments once per flip — pgraph.c:928 —
  and is exactly the "per displayed frame" clock.)
- **Feedback flush barriers**: emitted per *bind*, they doubled the render-pass
  count (each nondraw block breaks the open pass). Keyed per *redraw*
  (`flushed_draw_time`), 8/frame sufficed.

Generalized: before deduplicating, ask *"what does the consumer actually
observe?"* — then key on that clock, not on the producer's busiest counter.

### 1.7 Render-pass count as an intrinsic-vs-overhead discriminator

The correlation table (per-frame mspf vs copies vs renderpasses) repeatedly
falsified plausible theories:

- Copy-heavy frames: ~700 copies ≈ ~700 passes ≈ 190–250 ms.
- **Zero-copy frames with the same draw volume: still ~656 passes** — proving
  the passes were *genuine per-block target switches*, not copy-splits, and
  killing the "copies break pass merging" theory (partially — with the copies
  gone *and* the flush deduped, passes then merged to ~20, so both effects
  were real but separable).

Method: dump `(mspf, SURF_TO_TEX, PIPELINE_RENDERPASSES, BEGIN_ENDS)` per
frame and eyeball the correlation before believing any mechanism story.

### 1.8 The vblank quantization ladder

With a fixed-timestep engine syncing to 60 Hz vblank, frame time quantizes to
tick multiples: work ≤16.7 ms → 60 fps, ≤33.4 → 30, ≤50.1 → 20, ≤66.8 → 15.
LoC pinned at *exactly* med 50.0 ms = 3 ticks. Two uses:

- **Target setting**: "steady 20 → locked 30" is precisely "get total work
  under 33.4 ms" — an 11–17 ms shave, not a vague 50% speedup.
- **Diagnosis**: a med sitting *exactly* on a tick multiple is itself evidence
  the game is vblank-governed (matches the SB frame-limiter finding — one sim
  step per displayed frame).

### 1.9 Counter-attribution: classify at the decision point, then fix

The campaign's core method, applied four times with compounding payoff:

1. `S2T_MISS_{NOSURF,WITHIN,MIPS,PITCH,DIMS,CUBEMAP,FORMAT}` at the
   surface-as-texture bind → named the fallback classes (WITHIN+PITCH, not
   the guessed mips).
2. `S2T_REJ_*` inside the gather builder → revealed the gather engaging zero
   times and why (TEXSHAPE — swizzled textures).
3. `ZC_REJ_{FMT,ZETA_BOUND,ZETA_SURF,SHAPE}` at zero-copy eligibility →
   named the 731 surviving copies as **depth-surface binds** (12,182/window),
   pivoting the whole design to depth feedback.
4. Rate-limited **geometry dumps** (`XEMU_SURF2TEX_DEBUG`: tex/surface
   base+dims+pitch+format+swizzle per surviving miss) → turned counter classes
   into concrete shapes (the 1800×1800 D16 map, the 256×256 same-base block,
   the +0x80000 word-aligned interior bind).

Pattern: **every decision branch that rejects an optimization gets a reason
counter; every surviving slow path gets a rate-limited geometry dump.** The
cost is a handful of enum entries; the payoff is that each iteration *names*
its successor instead of guessing. Never fix ahead of the attribution — the
one time we did (the mips theory), the counters refuted it within one run.

### 1.10 Field observation beats counters for content correctness — twice

- **The smoke correlation**: seven owner screenshots showed slowdown tracking
  *smoke/haze/film-grain*, not VT count/distance/explosions. Two VTs clean:
  18 fps; one VT in a dust plume: 4. That observation — not a counter —
  selected the feedback hypothesis.
- **The transition smoking-gun**: the TV-static scene transition ran slow
  *before its destination scene was visible*, and full-speed before light
  scenes. Inference: the destination renders *under* the overlay, so cost
  tracks **rendered** content, not **visible** content. Confirmed the
  feedback-effect account from a completely independent angle.
- **The shadow bug**: caught *only* by eyes. Counters measure work, not
  meaning; a numerically-wrong-but-cheap frame looks perfect in every metric
  we had.

Rule adopted: performance claims can rest on counters; **correctness claims
require eyes** (or a pixel-diff harness we don't yet have — see tooling gaps).

### 1.11 "Engaged and benign" ≠ "inert" — regression gates report both

Steel Battalion under the new flags: frame profile identical (33.3 ms), no
errors — but its natural 99 fallback binds/window dropped to ~0 (served by the
new machinery) and its copy count fell 100×. That is *engaged and benign*, not
inert. The distinction matters for shipping language: "SB is unaffected" is
true only for the default-OFF path (byte-identical, gate-verified); with flags
on, SB's behavior *changes* — beneficially by every metric, but unreviewed by
eyes, so the flags stay off for SB until someone looks.

---

## Part 2 — Failure catalog (each with the check that would have caught it)

| # | Failure | Mechanism | The catch we now know to do |
|---|---|---|---|
| 1 | **"0 VK validation errors" was fiction, all campaign** | Validation gated on `display.vulkan.validation_layers`, never set; grep for messages that could not exist returned 0 = "clean" | A claim of zero must first prove the detector was armed (assert the layer's startup banner exists in the log before trusting grep-zero) |
| 2 | **Shadow double-decode** (v9.3) | See §1.2 | Trace decode chains for format reinterpretations; owner eyes on every content-touching change |
| 3 | **Missing prototype merged, broke modern GCC** | `pgraph_vk_surface_feedback_flush` undeclared; rig GCC 13 *warns* on implicit decls, cross GCC 16 *errors* (default since GCC 14) | Run the win64 cross build (the only modern-GCC check we have) before calling code done |
| 4 | **Per-bind flush doubled render passes** | Every ensure opened a nondraw block, breaking the open pass — 731 extra breaks/frame | Watch `PIPELINE_RENDERPASSES` before/after any change that records nondraw commands in the bind path |
| 5 | **Guest profiler measured the wrong scene** | The sample ring is *rolling*; the scene ended before the dump → post-scene menu idle diluted to "96% kernel idle," inverting the CPU story (mid-scene grab showed **86% game code**) | Snapshot the profile **mid-scene** (`cp` the bin while the scene runs); treat end-of-run dumps as suspect |
| 6 | **SDL3's log lines mistaken for xemu's** | SDL3's CMake subproject prints its own `Could NOT find LibUSB` (and X11/Wayland) lines, `\| `-prefixed, inside our configure log | Attribution rule: `\| `-prefixed lines belong to the CMake subproject; xemu's own results are the unprefixed `Run-time dependency …` / summary lines |
| 7 | **CLI `-loadvm` fails with the SBC** | The Steel Battalion controller is *hotplugged after* machine init; `-loadvm` restores before the device exists → `Unknown section or instance …usb-steel-battalion` | `loadvm` via the monitor socket after boot (all bench launchers do this) |
| 8 | **H-2 ghost match** | `pgrep -f "[q]emu-system-i386"` matched a *sibling command in the same ssh compound* whose argv named the binary (`strings …/qemu-system-i386`) | H-2 must be the *only* command in its ssh invocation (the `[q]` bracket defends only against pgrep itself) |
| 9 | **First v8 boot died on a table assert** | `pgraph_vk_transition_image_layout` supports an enumerated pair list; the new tracked-layout model generated `SHADER_READ→TRANSFER_SRC`, absent → `assert(!"unsupported layout transition!")` at boot | When adding a layout-state model, enumerate the *closure* of reachable pairs (or add a conservative catch-all — we did both: explicit pairs + a GENERAL catch-all) |
| 10 | **Python heredoc ate `\n` escapes** | Scripted edits via non-raw triple-quoted strings turned `\n` in C string literals into real newlines → "missing terminating"" | Raw strings / escaped backslashes in codegen scripts; grep `^");` after scripted edits |
| 11 | **libusb silently absent from packages** | `feature:'auto'` drops USB passthrough without failing the build; `build.sh` never forces it | `--enable-libusb` (fail loudly); verify the meson summary line per build — details in `HANDOFF-packaging.md` |

---

## Part 3 — Process patterns that carried the campaign

### 3.1 The deterministic benchmark snapshot

The owner's `savevm` of the storm scene (`vm-20260727054737`) converted a
"wait ~9 minutes for the attract to maybe roll into the heavy scene" loop into
a **90-second deterministic A/B**. Every verdict in rounds 2–3 ran on the
identical scene. Highest-leverage single artifact of the campaign. (Load it
via monitor, not CLI — failure #7.)

### 3.2 Drop-proof rig measurement: detached script + sentinel + poller

ssh sessions died mid-measurement twice. The pattern that survived:

```
rig:  setsid script.sh &        # script writes everything to a result file,
                                # ends with an "…-DONE" sentinel line
nas:  ssh rig 'until grep -qm1 DONE result; do sleep 15; done; cat result'
                                # run in background; re-runnable if ssh dies
```

The measurement itself never depends on the ssh connection living. Emulator
launches use `setsid` + output redirection for the same reason.

### 3.3 The iteration loop shape

`scp changed files → rig incremental build (~4 min) → launcher (boot +
monitor-loadvm into the snapshot) → 90 s counter window → verdict` — a
~10-minute hypothesis-to-data cycle, nine times. Two disciplines made it
converge: **never fix ahead of attribution** (§1.9), and **one lever per
build** so verdicts stay assignable.

### 3.4 Honest records are part of the deliverable

Three corrections were written *into* the permanent record rather than
silently fixed: the refuted "renderpasses are copy-splits" theory, the struck
validation claims (docs + a correction section), and the v9.3 revert with its
full mechanism. The commit history reads as what actually happened, including
the wrong turns — which is exactly what made the wrong turns cheap to inherit.

---

## Part 4 — Open threads (ranked)

1. **The five S2T/ALIAS levers specifically reach nobody by default — verified,
   not assumed (2026-07-28).** A field screenshot (below, item 4) matched the
   *pre-fix* baseline almost exactly, which raised the obvious question: is
   that user on an old build? Checked directly against the
   currently-published release: the AppImage's asset was replaced
   2026-07-28T00:44Z, well after the LoC merge, and its binary (`strings` on
   `qemu-system-i386`) **does contain** all five lever env-var names. So the
   fix ships. But all five default OFF (`docs/internal/A2-perf-levers.md`)
   and **nothing in the product turns them on** — no config UI, no per-title
   profile (the long-standing G10/T8 gap), and the AppImage's own launcher
   (`AppRun`) sets only `LD_LIBRARY_PATH`. So a plain launch with no manual
   env vars reproduces the pre-fix wall on this specific counter signature.
   **Correction (owner-reported, same day): this does NOT mean the campaign
   delivers no benefit** — a real user went from unable to reach gameplay at
   all to ~10 fps in-game, which is not explained by these five flags (still
   verified off) and has not yet been attributed to a specific cause (build
   fixes from this same period — the Windows cross-compile port, the libusb
   passthrough enforcement — are candidates; not yet confirmed). The accurate
   claim is narrower than what was first written here: these five specific
   levers are unreached by any normal launch, which remains worth fixing on
   its own merits, but "campaign delivers zero benefit" was an overstatement
   this field report disproves. Decision still worth making regardless:
   default these ON for LoC specifically (a per-title profile, once T8
   lands, or a title-ID-gated default in code — the same mechanism the
   game-hacks framework already uses, spec `docs/internal/A5-game-hacks.md`),
   since whatever is currently helping, these flags would help more.
2. **Validation-layer plumbing** — config set, layer installed, still no
   output. Until fixed, *nothing* here is validation-checked. Highest
   priority before this code travels beyond PenguinBox.
3. **The last 17 ms** (steady 20 → locked 30): frame work must fit under
   33.4 ms. Anatomy after the campaign: ~15 ms pfifo (method ~12, residuals) +
   guest time; no single villain. Candidates: the hot guest loop
   `0x33060–a0` (22% of mid-scene guest samples — an SB-radar-style RE
   target), texture-upload bursts (46–84 MB single-frame streaming spikes),
   reflection double-render frames (~2× draws, genuine work).
4. **SB with flags on** — engaged-and-benign per counters, unseen by eyes.
5. **Interactive combat** — the campaign benchmarked attract/cutscene scenes;
   nobody has yet piloted a mission under the fixed renderer. **2026-07-28
   field data point**: a screenshot from inside one such scene ("a replay",
   owner-supplied) shows MSPF 750 / renderpasses 751 — landing almost exactly
   on the *pre-fix* baseline (~767 renderpasses), not the v9.4 verdict
   (20–23). **That screenshot turned out to be from a Steam Deck, not the
   rig** — it has opened its own platform campaign,
   [`docs/perf/deck/00-baseline.md`](../deck/00-baseline.md), since every fix
   this document describes was only ever measured on the rig's NVIDIA
   hardware. The pre-fix numbers are now well explained (item 1, above) — the
   fix is present but never enabled by default — so the platform-specific
   questions (driver stack, no PRIME split, gamescope, hardware class) remain
   open but are no longer needed to explain *this specific* screenshot. Full
   write-up in `00-baseline.md` § Field observation (2026-07-28). (Input map
   note, corrected: the floor-test toml binds only the startup ritual; the
   "scancode 18 double-bound" claim here was a mischaracterization —
   `rtrigger` and `oxygen_supply_system` sit in *different, driver-selected*
   maps and can never both fire (B6-input-stack.md P3). The real input gap is
   that main/sub-weapon and lock-on are mouse-hardwired with no keyboard
   binding at all.)
6. **Upstreaming to xemu-project** — needs: working validation, a multi-title
   sweep, comment cleanup (the code reads as a research log), and squashing
   the revert pairs (v9.3+v9.4) into a clean series.
