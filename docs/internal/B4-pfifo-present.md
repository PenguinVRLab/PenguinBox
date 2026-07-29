# Spec — pfifo / command processing / present

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| **Tier** | B (inherited, modified by us) |
| **Code** | `hw/xbox/nv2a/pfifo.c` (581 L), `pgraph/pgraph.c` (3489 L, dispatch/flip methods), `pgraph/vk/draw.c` (3002 L, renderpass + submit/finish), `pgraph/vk/renderer.c` (flip_stall, process_pending), `pgraph/vk/display.c` (present composite), `pgraph/vk/reports.c` (occlusion reports), `pgraph/vk/command.c` (pools), `nv2a.c` (vblank IRQ), `pgraph/phase_timers.{c,h}` |
| **Status** | v1 draft |
| **Last verified** | 2026-07-27 (anchors re-checked against tree at `caf0a8eaf0`) |

## Purpose & scope

This subsystem is the **engine loop of the emulated GPU**: it consumes the
guest's pushbuffer (PFIFO), dispatches NV2A methods to PGRAPH, turns draw
methods into Vulkan renderpasses/pipelines/submissions, and implements the
flip/present contract that paces the guest. It is where frame time is *made* —
every perf campaign so far ended up here. In scope: the pfifo thread and its
loop, pushbuffer parsing, method dispatch, the draw_time/frame_time clocks,
renderpass open/close discipline, the submit/fence ring, `pgraph_vk_finish`
and every reason it fires, the flip-stall/vblank pacing mechanism, and the
report (occlusion query) drain. Out of scope: what surfaces/textures *are*
(B1/B2), image layouts (B5), the per-lever contracts of the overlap stack
(A2), the display swapchain/VR compositor internals (A1), GL backend (B9).

## External interface

**Guest-facing (the emulated hardware contract):**
- PFIFO MMIO regs (`pfifo_read`/`pfifo_write`, pfifo.c:38–91) — CACHE1
  DMA_PUT/GET/STATE etc.; writes kick the engine (`pfifo_kick`, pfifo.c:93).
- The pushbuffer: guest RAM parsed by the DMA pusher (pfifo.c:252) —
  inc/non-inc method packets, old-jump/jump/call/return flow (pfifo.c:362–428,
  one subroutine level).
- NV097 methods via RAMHT object binds (method 0) and engine methods
  (≥0x100), dispatched by the puller (pfifo.c:150–244) into `pgraph_method`.
- The flip family: `SET_FLIP_READ/WRITE/MODULO`, `FLIP_INCREMENT_WRITE`,
  `FLIP_STALL` (pgraph.c:894–943) and `NV097_GET_REPORT`
  (ZPASS_PIXEL_CNT only — asserted, reports.c:229–230).
- VBLANK IRQ: raised in `nv2a_vga_gfx_update` (nv2a.c:197–207) on the QEMU
  display-refresh callback.

**Host-facing:**
- Env levers hooking this machinery (contracts in **A2**, hook sites here):
  `XEMU_FRAME_OVERLAP` (slot-ring retire decomposition, draw.c:1813–1866),
  `XEMU_ASYNC_PRESENT` (flip + composite submit-no-wait, renderer.c:192–225,
  display.c:1286), `XEMU_ASYNC_REPORTS` (marker + timed park, reports.c:555+,
  pfifo.c:499–516), `XEMU_ASYNC_QUERIES` (QUERY_ASYNC submit-only resolve,
  reports.c), `XEMU_BATCH_QRESET` (in-pass queries + region ping-pong,
  draw.c:1623–1630, 1898–1910), `XEMU_PIPE_OPT` (draw.c:809),
  `XEMU_NARROW_FENCE`, `XEMU_ELEMENT_BATCH`/`XEMU_INLINE_BATCH` (pusher
  lookahead consumers), `XEMU_PHASE_TIMERS` (A3), `XEMU_SURF_ATTR`
  (renderer.c:256–269). All default-OFF with `XEMU_NO_*` force-off.
- Phase-timer output (`phase:` / `pfifo:` stderr lines) — see Instrumentation.
- `xbox_game_hacks_frame_tick()` — per-frame hook point at flip
  (renderer.c:245, contract in A5).

## State & data structures

- **`d->pfifo`** — regs array (the CACHE1 state IS the parse state: DMA_STATE
  carries method/subchannel/count between pusher iterations), `lock`,
  `fifo_cond`/`fifo_idle_cond`, `fifo_kick`, `halt`, the thread handle.
- **`d->pgraph` clocks** — `draw_time` (uint64): incremented once per
  completed VK draw (draw.c:2304), per blit (vk/blit.c:176) and per
  surface-touching clear (vk/surface.c:2246). **The per-draw clock.**
  `frame_time` (uint64): incremented in exactly one place —
  `FLIP_INCREMENT_WRITE` (pgraph.c:928). **The per-flip clock.** Both start 0
  (pgraph.c:254–255), never reset at runtime.
- **Stall flags** — `waiting_for_flip` (set pgraph.c:942, cleared
  pfifo.c:135), `waiting_for_nop`, `waiting_for_context_switch`; FIFO access
  gate = `NV_PGRAPH_FIFO_ACCESS` (pfifo.c:99–102).
- **The submit ring** — `r->cb_fence_ring[CB_FENCE_RING_SIZE]` of
  `CbFenceSlot` {main `cb`, `aux_cb` (staging sync), per-slot binary `sem`,
  `fence`, `submit_index` (`SURFACE_NO_WRITE` = never used), the slot's
  absolute occlusion-query range, frame-overlap retire lists}. `submit_count`
  is the monotonically increasing submission index; slot = ring[idx % SIZE].
- **Renderpass state** — `RenderPassState` {color_format, zeta_format,
  color_general, zeta_general} (init draw.c:399–414); cached VkRenderPass
  array keyed by memcmp (draw.c:530–538); `r->in_render_pass`,
  `r->in_command_buffer`, `r->pipeline_binding` (LRU pipeline cache keyed by
  {render_pass_state, shader_state, 9 fixed-function regs, vertex
  descriptions} — draw.c:798–807).

## Invariants

- **I1 — Single engine thread.** All pushbuffer parsing, method dispatch, VK
  recording, submission, fencing, and the present composite run on the one
  pfifo thread (created nv2a.c:239, loop pfifo.c:453–531). vCPU threads only
  write regs/RAM and `pfifo_kick`. [DERIVED — thread creation + no other
  callers of the dispatch path; consistent with every campaign observation]
- **I2 — Lock discipline: never both.** The pfifo thread never holds
  `pfifo.lock` and `pgraph.lock` simultaneously; every crossing drops one
  before taking the other (pfifo.c:189–203/224–234, renderer.c:161–176,
  pgraph.c:3424–3454). [DERIVED — all sites follow the drop-then-take shape]
- **I3 — The two clocks.** `draw_time` advances per draw/blit/clear;
  `frame_time` advances once per guest flip and nowhere else
  (pgraph.c:928 is the only increment). **Any cache/dedup key must use the
  clock matching its consumer's cadence** — keying a per-frame need on
  draw_time re-does the work per draw (the LoC 700→1–2 depth-copy fix).
  [VERIFIED — LoC v9 counter A/B: per-frame key collapsed SURF_TO_TEX]
- **I4 — Flip pacing is guest-driven.** After `FLIP_STALL`, the puller stalls
  (pfifo.c:126–148) until `READ_3D != WRITE_3D` (pfifo.c:107–124). Only the
  guest advances READ_3D (`SET_FLIP_READ`, pgraph.c:894), which its vsync ISR
  does at VBLANK-IRQ cadence; the emulator never advances it. [VERIFIED —
  SB lane-2 RE traced the guest DPC; docs/sb-frame-limiter.md]
- **I5 — The quantization ladder.** VBLANK fires from the display refresh
  callback (nv2a.c:203) on a ~16.7 ms tick; because of I4, a frame whose
  pfifo work spans N ticks completes in exactly N·16.7 ms. Frame-time
  medians therefore land on 16.7/33.4/50.0/66.7… — med 50.0 = 20 fps means
  "work fits in (33.4, 50.0]"; getting under 33.4 snaps to locked 30.
  [VERIFIED — LoC med exactly 50.0; SB med exactly 33.3; ladder observed all
  campaign]
- **I6 — Submission order = retirement order.** One VkQueue; submissions
  indexed by `submit_count`; a slot pushed out of the ring is long-complete
  (`wait_for_submission`, draw.c:1598–1611 relies on it). [DERIVED — single
  queue + fence-per-slot; no cross-queue sync exists]
- **I7 — Renderpasses stay open across draws until dirtied.** A draw reuses
  the bound pipeline+pass unless `check_pipeline_dirty` (draw.c:794–851)
  fires: no binding, shader change, texture change (suppressed under
  PIPE_OPT — texture *image* data never keys the pipeline, draw.c:798–807),
  render_pass_state change (draw.c:781–791), one of 9 fixed-function regs, or
  vertex-layout change. Every finish/submit, clear, and nondraw command also
  closes the pass. [DERIVED — the dirty walk; PIPE_OPT claim VERIFIED by
  NV2A_PROF_PIPELINE_LOOKUP_SKIP A/B in the SB matrix]
- **I8 — Renderpass compatibility across layout variants.** The
  color_general/zeta_general variants differ from the base pass **only** in
  attachment layouts (draw.c:428–466); Vulkan renderpass compatibility
  ignores layouts, so **framebuffers** created against one variant are legal
  with the others (framebuffers are NOT keyed by variant — draw.c:541+ uses
  `r->render_pass` current value). Pipelines ARE per-variant (key includes
  render_pass_state) — compatibility is exploited for framebuffers, not
  pipelines. [VERIFIED — v8/v9 machinery field-proven on LoC; per Vulkan
  spec §renderpass-compatibility]
- **I9 — Attachment contents always survive pass boundaries.** All cached
  passes use LOAD_OP_LOAD / STORE_OP_STORE (+ stencil load/store for zeta)
  (draw.c:434–459) — a pass break is semantically free (contents preserved),
  only *costly*. [DERIVED from the attachment descriptions]
- **I10 — Rest-layout restore at pass entry.** `begin_render_pass` transitions
  any bound target not in its rest layout back to it before
  vkCmdBeginRenderPass (draw.c:1514–1531) — the hook that makes zero-copy
  borrowing legal (B5 owns the layout model). [VERIFIED — boot assert would
  fire otherwise; LoC runs clean]
- **I11 — A finish is a full drain.** `pgraph_vk_finish` = submit_batch +
  wait **all** slots + recycle (arena resets draw.c:1848–1879, framebuffer
  destroys, descriptor index reset) + report resolve (draw.c:1896) [+ S4
  region swap only at FLIP_STALL, draw.c:1898–1910]. `submit_batch` alone
  ends query→pass (order per batch_qreset, draw.c:1623–1638), records the
  aux staging-sync CB, and two-element-submits {aux signals slot sem, main
  waits it} with the slot fence (draw.c:1662–1703). Arena/bitmap resets
  happen ONLY in recycle after a true full drain. [DERIVED — code path;
  parity VERIFIED by the S1 OFF-matrix (byte-identical)]
- **I12 — Queries never span a submit.** With batch_qreset: in-flight query
  ends *inside* the pass (assert draw.c:1564); without: pass closes, then
  query. Either way vkEndCommandBuffer never sees an active query.
  [DERIVED — assert + both orderings in submit_batch]
- **I13 — The pusher's lookahead is the batching interface.** The puller
  passes `parameters`/`num_words_available`/`max_lookahead_words` through to
  `pgraph_method`, which may consume >1 word (return = words processed,
  pfifo.c:339–356). ELEMENT/INLINE batching lives entirely behind this
  return-count contract; the pusher trusts it for DMA_GET advance.
  [DERIVED; OFF-parity VERIFIED in the E-lever A/B]
- **I14 — Report marker forbids the untimed park.** With an async-report
  marker pending, the idle park uses a ~1 ms timedwait; the untimed park is
  asserted marker-free (pfifo.c:499–516) — the guest spin-reading report RAM
  will never kick the condvar. [VERIFIED — armed assert in-tree; hang class
  reproduced during S3 bring-up]

## Policies & decision points

- **Loop order** (pfifo.c:462–520): process_pending (downloads / dirty-surface
  downloads / sync / flush handoffs from other threads — renderer.c:152–178)
  → pusher (unless `halt`) → report drain → park if no kick. Priority
  consequence: pending downloads/flushes preempt new method work each
  iteration.
- **Puller stall set** (pfifo.c:143–148): flip stall, waiting_for_nop,
  waiting_for_context_switch, FIFO access off. Stall returns −1: the pusher
  breaks and re-enters after the next kick.
- **Finish reasons** — the complete taxonomy (enum renderer.h:1114–1132,
  counter map draw.c:1570–1581). Raise sites and meaning:
  - `VERTEX_BUFFER_DIRTY` (vertex.c:60) — guest wrote vertex RAM the open
    batch reads; must drain before reupload.
  - `SURFACE_CREATE` (surface.c:1490–2009, several) — surface
    create/evict/migrate needs prior GPU work retired (B1 owns the policy).
  - `SURFACE_DOWN` (surface.c:259–1066, several) — GPU→RAM readback
    requires the producing work drained. The LoC storm class.
  - `NEED_BUFFER_SPACE` (draw.c:550/2069/2653, shaders.c:263,
    texture.c:830/969, surface.c:264/1552) — staging-arena/descriptor
    exhaustion backstop; self-heals capacity.
  - `FRAMEBUFFER_DIRTY` — **no live raise site** (only the enum/counter
    table). Vestigial. [DERIVED — grep of tree]
  - `PRESENTING` (display.c:1295/1460/1463) — composite needs the scanout
    surface's draws submitted (async present: submit-only, no wait).
  - `FLIP_STALL` (renderer.c:215/224) — the frame-boundary drain; the
    async-present branch demotes it to submit-only + moves the VMA budget
    tick to the flip site (renderer.c:216–222).
  - `FLUSH` (renderer.c:95, surface.c:736, reports.c:730) — external
    quiesce: savevm, renderer switch, dirty-surface download pass.
  - `STALLED` (reports.c:663/836) — guest spinning on an unresolved report;
    the classic SB read-trap.
  - `QUERY_ASYNC` (reports.c, 5 sites) — submit-without-drain resolve so a
    scoped wait can read query results; never passed to finish
    (renderer.h:1124–1131).
- **The S2+S4 composition interlock** (renderer.c:201–224): async-present
  yields to batch-qreset at the flip — when both are on, the flip keeps the
  synchronous finish because the query-region ping-pong swap is only proven
  safe after a full drain. Removing this requires the region-scoped window
  model flagged at draw.c:1902–1908.
- **Present path**: SDL/GL thread requests a frame → `sync_pending` →
  pfifo thread `pgraph_vk_sync` → `pgraph_vk_render_display`
  (renderer.c:133, display.c:1681–1711) → composite from the scanout
  surface; async variant `render_display_async` (display.c:1286) submits
  PRESENTING batches without waiting and interops with GL via the
  CPU-complete contract (no VK↔GL semaphore, by design —
  display.c:1275–1284).

## Failure modes

- **Missing kick → stall forever.** Any new wait added to the loop must be
  kicked by whoever satisfies it; the report-marker park bug (I14) is the
  canonical instance. Symptom: guest spins, pfifo idle ~100%.
- **Flip-stall deadlock**: if the guest's vsync ISR dies (or VBLANK IRQ
  stops), READ_3D never advances and the puller stalls at FLIP_STALL
  permanently. Diagnostic: `waiting_for_flip` set + READ==WRITE.
- **Pass-break storms are the #1 cost class**: LoC pre-fix ran ~767
  renderpasses/frame with copy≈rpass≈mspf correlation [VERIFIED — the
  campaign correlation tables]. Any feature that interleaves non-draw VK work
  between draws (S2T copies, downloads) multiplies pass count; check
  NV2A_PROF_PIPELINE_RENDERPASSES before/after.
- **Asserts that are load-bearing**: reserved pushbuffer command
  (pfifo.c:427) and DMA error → pusher suspension (pfifo.c:441–450); RAMHT
  invalid-entry asserts (pfifo.c:179–181); in-pass query under batch_qreset
  (draw.c:1564); park-with-marker (pfifo.c:514).
- **Recycle-too-early**: resetting arenas/bitmap while a slot is in flight
  reintroduces the WAR race the era model exists to catch — the frame-overlap
  recycle defers unless ALL fences signaled (draw.c:1827–1866;
  OVERLAP_RECYCLE_DEFER must read 0 under S1-alone — armed detector).
- **Double-attribution in phase timers**: the submit leaf is depth-guarded —
  finishes nested inside download/upload/query/present leaves are absorbed by
  the enclosing op (draw.c:1760–1766). Naive additions of new leaves must
  respect the nesting or `pfifo:` sums will exceed pfifo_work.

## Instrumentation

- **Phase timers** (`XEMU_PHASE_TIMERS=1`, A3 owns format): per-frame
  `phase:` line (mspf, run/blocked, pfifo_work/idle, present) + `pfifo:`
  line decomposing pfifo_work into disjoint leaves — texhash / upload /
  download / **submit** (top-level finishes) / queries / **present** (with
  gpuwait/composite/vrsubmit sub-thirds) + method (pusher minus leaves,
  pfifo.c:474–480) + residual (phase_timers.h:157–190, 370–388). This is the
  triage instrument: download-dominant → B1/B2 levers; present/gpuwait →
  overlap stack; method → guest-side cost.
- **Counters** (`hw/xbox/nv2a/debug.h`): `NV2A_PROF_FINISH_*` per reason,
  `QUEUE_SUBMIT`, `PIPELINE_RENDERPASSES` (draw.c:1512), `PIPELINE_NOTDIRTY`
  / `PIPELINE_LOOKUP_SKIP`, arena `ARENA_*_KB`/peak + `DESC_MAX`
  (draw.c:1786–1809), `OVERLAP_RECYCLE_FULL/DEFER`, report counters
  (`REPORT_FORCE_DRAIN`, `REPORT_ASYNC_RETIRE`, `REPORT_AGEOUT_SUBMIT`,
  `QUERY_ASYNC_RESOLVE`). FPS overlay = flip rate (Debug→Video menu).
- **`XEMU_SURF_ATTR=1`**: every 30 flips, per-surface read/download
  attribution lines (renderer.c:256–269) — names *which* surface carries
  readback cost.
- How to tell the machinery is live from a log: `phase:` lines present;
  FINISH_FLIP_STALL ≈ frame count; FINISH_* distribution names the wall.

## Verified-by

- SB overlap matrix: stage-by-stage A/B with OFF-parity (default path
  byte-identical) — the S1–S5 gate rounds (docs/vr/present-wall-plan.md,
  branch history). Re-run form: rig `~/sb-regression-check*.sh`.
- LoC bench set: `~/loc-bench-{off,ext,zc,alias,fb}.sh` → counter totals +
  per-frame correlation + mspf distribution; the pass-count/mspf correlation
  and the ladder medians come from these reports.
- SB frame-limiter RE (docs/sb-frame-limiter.md) — guest-side flip cadence
  ground truth for I4.
- Planned: T3 `perf-bench.sh` freezes the report format; T11 counter
  baselines gate FINISH_*/renderpass counts per title.

## Confidence & unknowns

- [ASSUMED] The vblank tick is a stable 16.7 ms on all hosts/configs — the
  QEMU display-refresh cadence driving nv2a.c:203 has not been audited under
  vsync-off, headless, or throttled-display conditions; the ladder's
  *quantum* could drift where the console refresh does.
- [ASSUMED] The guest-side READ_3D advance contract (I4) generalizes beyond
  the two RE'd titles — it is the standard D3D vsync DPC, but only SB (and
  LoC by observation) are verified.
- [ASSUMED] FRAMEBUFFER_DIRTY is dead code — no raise site found; if a
  future upstream merge reintroduces one, its counter semantics resume
  silently.
- [DERIVED, unexercised] Multi-channel/context-switch paths
  (`pgraph_context_switch`, RAMHT channel_id checks are commented out —
  pfifo.c:180/214) — retail titles observed use one channel; behavior under
  real multi-channel use is untested.
- [ASSUMED] KVM-path interactions: async present pins itself to TCG
  (display.c:246); the sync path under KVM is believed identical but has no
  gate run.
- Open question: whether the composite (`sync_pending`) cadence can starve
  the pusher on slow hosts (composite runs in the same loop) — never
  observed, never measured.
