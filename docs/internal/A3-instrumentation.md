# Spec — Instrumentation

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| A | `hw/xbox/nv2a/pgraph/phase_timers.{c,h}`, `hw/xbox/nv2a/debug.h`, `hw/xbox/nv2a/pgraph/profile.c`, `accel/tcg/xemu-guest-profile.c`, `hw/xbox/nv2a/pgraph/vk/{texture,surface,renderer,instance,debug}.c`, `hw/xbox/nv2a/pgraph/debug_renderdoc.c`, `ui/xui/debug.cc`, `hw/xbox/nv2a/pgraph/vk/vr_sbc_panel.c` | v1 draft | 2026-07-27 |

## Purpose & scope

The reference contract for every measurement instrument in the tree: how each
is armed, its exact output, how to read it, its known traps. Covered: phase
timers, NV2A perf counters (`nv2a-prof`), the Video Debug overlay, the guest-PC
profiler, S2T geometry dumps, surface access/dump/attr logging, Vulkan
validation, the RenderDoc hook, `XEMU_VR_PANEL_DUMP`. Out of scope: the perf
*levers* (spec A2), offline analysis beyond arming/reading
(`tools/perf/README.md`), QEMU trace events, the patch-only `scan-pc-capture`
diagnostic. Normative: a claim quoting an instrument in violation of these
invariants is void.

## External interface (every env flag and config key, with arming semantics)

All env gates are **presence-checked once and cached** (getenv-once); changing
the environment after start has no effect; any value arms — `FLAG=0` still
arms unless stated otherwise.

| Flag / key | Arms | Semantics | Site |
|---|---|---|---|
| `XEMU_PHASE_TIMERS` | phase timers | presence = on; banner `phase: XEMU_PHASE_TIMERS on — per-60-flip frame-phase decomposition enabled`; OFF path is one cached atomic read + branch, no clock calls | `phase_timers.c:132-144`, `phase_timers.h:127-134` |
| *(none — always on)* | `nv2a-prof:` stderr line | one line every 60 frames, unconditional | `profile.c:58-74` |
| *(none — always on)* | Video Debug overlay data | `g_nv2a_stats` always accumulates; the window is UI-toggled | `profile.c:22`, `debug.cc:346` |
| `XEMU_GUEST_PROFILE=N` | guest-PC profiler | N = sample interval in TBs (default 4096 if non-numeric/≤0); constructor-time arming; banner `[guest-profile] ARMED: interval=%u TBs, flush every %u flips, out=%s (chaining forced off while armed)` | `xemu-guest-profile.c:188-221` |
| `XEMU_GUEST_PROFILE_DUMP_FLIPS` / `_PATH` | (modifiers) | flush ring every K flips (default 300); output file (default `guest-profile.bin` in emulator CWD) | `xemu-guest-profile.c:203-214` |
| `XEMU_SURF2TEX_DEBUG` | s2t-miss geometry dumps | presence = on, **but lines only fire on `XEMU_SURF2TEX_EXT` runs** (see F-4); rate-limited ≤20 lines / 5 s | `vk/texture.c:1536-1578`, call site `2315-2328` |
| `XEMU_SURF_ACCESS_LOG` | surface access logger | presence = on; first **200** intercepted accesses logged, then silent forever | `vk/surface.c:792-827` |
| `XEMU_SURF_DUMP` | surface PPM dumps | `auto` = every color surface ≤512×512 on download; `<hex addr>` = pin one base; writes `/home/pacarey/surf-dump-<addr>-<WxH>.ppm` (hardcoded path — rig-only) | `vk/surface.c:599-641` |
| `XEMU_SURF_ATTR` | per-surface attribution | presence = on; one `surf-attr:` line per active surface every 30 flips | `vk/renderer.c:251-273` |
| `display.vulkan.validation_layers` | VK validation | config key (TOML), default false; **BROKEN as of 2026-07-27 — no output even when true** (tooling-gaps G1) | `config_spec.yml:473`, `vk/instance.c:205-229` |
| `display.vulkan.assert_on_validation_msg` | (modifier) | abort on any validation warning/error via the debug callback | `vk/instance.c:52-56`, `config_spec.yml:475` |
| F10 key (+Shift=5 frames, +Ctrl=trace) | RenderDoc capture | build-gated `CONFIG_RENDERDOC`; needs `librenderdoc.so` loadable and the RenderDoc UI attached (`IsTargetControlConnected`) | `ui/xui/menubar.cc:88-95`, `vk/debug.c:43-70`, `debug_renderdoc.c:41-97` |
| `XEMU_VR_PANEL_DUMP=<path>.ppm` | SBC panel dump | re-rasters + rewrites the PPM on every panel content change (UI thread); verifies panel CONTENT without a headset — placement is XR-only (spec A1) | `vk/vr_sbc_panel.c:460-483` |

Scriptable read paths: phase lines and `nv2a-prof:` land on **stderr** (rig
launchers redirect to a log); `grep 'phase:' log | grep -oE 'mspf=[0-9.]+'` is
the scripted FPS equivalent (`runbooks/rig-perf-measurement.md` §instrument);
the ladder/AB scripts scrape `nv2a-prof:` (`tools/perf/README.md`).

### 1. Phase timers — three stderr lines every 60 flips

Emitted from the flip anchor `nv2a_phase_flip()` in `pgraph_vk_flip_stall`
(`vk/renderer.c:294`); the first-ever flip only baselines (`phase_timers.c:512-527`).
All figures are **per-frame means in ms over the just-completed 60-flip
window** (≈ every 2 s at 30 fps).

`phase: mspf=%.1f[%.1f-%.1f] run=%.1f blocked=%.1f dl_stall=%.1f(%.1fx) dl_proc=%.1f pfifo_work=%.1f pfifo_idle=%.1f present=%.1f light=%u heavy=%u` (`phase_timers.c:439-444`)

- `mspf` — mean wall flip-to-flip ms, `[min-max]` over the window; 33.3 = 30
  fps. `run` = mspf − blocked: guest vCPU actually executing (clamped ≥0).
- `blocked` — all bracketed vCPU blocking waits: the surface-download wait
  (`vk/surface.c:764-769`; non-external-memory variant `675-694`) + the
  fifo-quiesce MMIO wait (`nv2a.c:271-282`). Fat `blocked − dl_stall` gap =
  MMIO-quiesce cost.
- `dl_stall(Nx)` — the surface-download round-trip subset; N = blocking
  round-trips per frame. **The guest sleeping on GPU→RAM readback** — the
  number that drove the SB and LoC download triage.
- `dl_proc` — pfifo-side download processing (`pgraph_vk_process_pending_downloads`,
  `vk/surface.c:702-714`); vs dl_stall it splits copy from kick latency.
- `pfifo_work` = mspf − pfifo_idle: pfifo/GPU-emulation-thread real work — the
  number that **is** the frame on a GPU-walled title. `pfifo_idle` — parked on
  `fifo_cond` (`pfifo.c:494-519`).
- `present` — flip-time GPU drain + VR submit (`vk/renderer.c:118-146,180-232`).
  `light`/`heavy` — frame counts under/over `PHASE_HEAVY_MS` = 40 ms
  (`phase_timers.h:115`).

`pfifo: work=… method=… texhash=… queries=… download=… upload=… submit=… present=… residual=…` (`phase_timers.c:464-468`) — disjoint decomposition of
pfifo_work; residual ≥ 0 by construction:

- `method` — pushbuffer/pgraph-method dispatch **self** time: the
  `pfifo_run_pusher` span minus all leaf children (`pfifo.c:468-481`,
  `phase_timers.c:247-270`). The per-draw Vulkan recording cost.
- `texhash` — `fast_hash` texture-content hashing (`vk/texture.c:2448`);
  `queries` — occlusion-query drain, `pgraph_process_pending_reports`
  (`pfifo.c:487`).
- `download` — surface GPU→RAM copy incl. its nested SURFACE_DOWN finish
  (`vk/surface.c:234`; every trigger path funnels here). **This bucket, not
  `dl_stall`, measures the copy itself.** `upload` — RAM→GPU upload incl.
  WAW/WAR fence waits + SURFACE_CREATE finish (`vk/surface.c:1460`).
- `submit` — a **top-level** (un-nested) `pgraph_vk_finish` = mid-pushbuffer
  mandatory GPU flush (`vk/draw.c:1766`).
- `present` — the present leaf: `render_display` composite + VR submit
  (`vk/renderer.c:126`, from `pgraph_vk_sync`, outside the pusher) + the
  flip-finish drain (`vk/renderer.c:189`). Same quantity as `phase:` present.
- `residual` — pfifo_work − sum(above): pfifo-loop glue.

`pfifo2: method_synctex=… method_const=… method_attr=… method_pipe=… method_desc=… method_draw=… method_rpchurn=… method_resid=… present_gpuwait=… present_composite=… present_vrsubmit=… present_resid=…` (`phase_timers.c:503-509`) — second level, cracking the two dominant buckets:

- `method_synctex` — the **blocking** `vkWaitForFences` in
  `end_single_time_commands` during texture bind (`vk/command.c:163`): the
  deferrable GPU wait, vs genuine CPU recording elsewhere.
- `method_const` — per-draw uniform recompute/hash/UBO append (`vk/draw.c:893`).
  `method_attr` — vertex-attr bind + inline staging build (`2829,2854`).
  `method_pipe` — texture bind + pipeline LRU + renderpass/framebuffer +
  begin_draw binds (`895,2037,2129`). `method_desc` — `vkUpdateDescriptorSets`
  (`2095`). `method_draw` — `vkCmdDraw[Indexed]` + buffer binds + end_draw
  (`2859,2903`).
- `method_rpchurn` — query-driven render-pass churn carved out of PIPE
  (`vk/draw.c:2164-2192`); the present-wall Lane-2 target. `method_resid` —
  inter-draw register-dispatch self-time + un-bracketed glue (clears: their
  cursor marks are dropped, `phase_timers.c:318-328`).
- `present_gpuwait` — `pgraph_vk_finish(FLIP_STALL)` drain (`vk/renderer.c:226-232`);
  under `XEMU_ASYNC_PRESENT` the same bracket times the flip-time `submit_batch`
  instead (`vk/renderer.c:213-232`) — meaning changes with that lever.
  `present_composite` — `pgraph_vk_render_display` (`vk/renderer.c:129-135`).
  `present_vrsubmit` — `xemu_vr_frame` XR copy+submit (`vk/renderer.c:141-146`).

### 2. NV2A perf counters — `nv2a-prof:` line + overlay

Every 60th frame, unconditionally, one stderr line with mspf and **all
non-zero counters of that single just-completed frame** (`profile.c:58-74`):
`nv2a-prof: mspf=%d NAME=%d NAME=%d …`. `mspf` here = flip-increment →
FLIP_STALL render ms (`profile.c:44-47`), not the phase-line wall mspf. The
1536-B buffer's `n < size-48` append guard silently drops tail counters on a
too-long line; sized so the overlap-detector tail currently fits
(`profile.c:59-68`); re-check on every counter addition (M-3).

Counter families (`debug.h:124-243`); label = enum minus `NV2A_PROF_`:

- **FINISH_*** — `pgraph_vk_finish` reasons (why the GPU pipeline drained).
- **QUEUE_SUBMIT{,_AUX,_1..5}**, **PIPELINE_***, **BEGIN_ENDS**,
  **DRAW_ARRAYS**, **INLINE_{BUFFERS,ARRAYS,ELEMENTS,BATCH}**,
  **ELEMENT_BATCH** — draw/submit volume. **QUERY*** / **REPORT_*** —
  occlusion-query machinery (async-queries campaign). **SHADER_***,
  **UNIFORM_SKIP**, **ATTR_BIND**, **GEOM_BUFFER_UPDATE_*** — per-draw state
  churn. **TEX_UPLOAD{,_KB}**, **UPLOAD_INBATCH**, **UPLOAD_STAGING_WAIT**,
  **TEX_HASH{,_KB,_SKIPPED}** — texture upload/hash volume.
- **SURF_CREATE** (`vk/surface.c:1326`), **SURF_DOWNLOAD** (`vk/surface.c:236`),
  **SURF_UPLOAD** (`vk/surface.c:1470`), **SURF_TO_TEX** (`vk/texture.c:972`
  color copy, `1179` zeta-convert copy; GL parity `gl/surface.c:325`),
  **SURF_TO_TEX_FALLBACK** (`vk/texture.c:2305`, only when the bind actually
  forced a download), **SURF_{CPU_WRITE,CPU_READ,DMA_WRITE}**
  (`vk/surface.c:785-790`), **SURF_PREDL**, **SURF_CPU_DL_WAIT**,
  **SURF_SWIZZLE**, **2D_BLIT** — surface-cache traffic.
- **Phase-0 emulation overhead** (`debug.h:80-95`): TLB_FLUSH_ALL,
  MEMCB_REGISTER, MEMCB_{READ,WRITE}_DISPATCH, TB_TRANSLATE, TB_RECYCLE,
  TLB_FILL, MMIO_DISPATCH — cross-file increments (`system/physmem.c`,
  `accel/tcg/cputlb.c`, `translate-all.c`, `nv2a_int.h`).
- **Present-wall S0/S1** (`debug.h:97-122`): ARENA_{IDX,VTX,UNI}_KB (+_PK_KB
  high-water), DESC_MAX{,_COMP}, TEX_UPLOAD_KB, VTX_RAM_SYNC_KB,
  VTX_RAM_WAR_WAIT, OVERLAP_{RECYCLE_FULL,RECYCLE_DEFER,SLOT_RETIRE,FB_DEFER,
  DESC_RESET}, ASYNC_PRESENT_{SUBMIT,GATE_WAIT,REUSE_WAIT}. KB/MAX entries are
  magnitude counters, not event counts. OVERLAP_RECYCLE_DEFER must stay 0
  under S1-alone — an armed regression detector.
- **2026-07 S2T set** (LoC; classify-at-the-decision-point, `docs/perf/loc/02-lessons-learned.md` §1.9):
  - `S2T_MISS_{NOSURF,WITHIN,MIPS,PITCH,DIMS,CUBEMAP,FORMAT}` — WHY a
    surface-fed texture bind missed the GPU copy path; ticked **only when the
    fallback actually downloads** (forced>0, `vk/texture.c:2146-2202,2304-2312`;
    NOSURF refined to WITHIN only on the storm path, `2308-2311`).
  - `S2T_EXT_{COPY,QUAD,BOUNCE,REGIONS,PARTIAL}` — the extended GPU paths:
    COPY = any ext copy (`1589,1724,1790`); QUAD/BOUNCE tag the variant;
    REGIONS accumulates gather region count (`1590-1591`, magnitude);
    PARTIAL = gather without full row coverage (`1593`).
  - `S2T_REJ_{TEXSHAPE,SWIZSURF,PITCH,BPP,ALIGN,OVERFLOW,NONE_RESIDENT}` — why
    the gather builder rejected; ticked only on EXT runs at the fallback site
    (`vk/texture.c:2315-2326`).
  - `ALIAS_KEPT` — a large parent surface kept (not evicted) under a small
    incompatible child: invalidate-overlaps leg `vk/surface.c:1159`;
    target-switch leg `2128`.
  - `ZC_REJ_{FMT,ZETA_SURF,SHAPE}` + `ZC_FLUSHES` — zero-copy borrow
    eligibility rejects (`vk/texture.c:2362,2368,2380`) and pre-sample flushes
    (`1392`). **`ZC_REJ_ZETA_BOUND` (`debug.h:240`) has NO increment site** —
    the v9.4 depth-borrow revert removed its leg; it reads 0 forever.
- **RADAR_SCAN_SKIP** — game-hack self-counter. Reading: the overlay Advanced
  tree plots each counter over the 300-frame history with a `NAME: value`
  legend of the last completed frame (`ui/xui/debug.cc:447-458`,
  `profile.c:91-97`).

### 3. Video Debug overlay (the FPS instrument)

Open: move the mouse to wake the auto-hiding menu bar (5 s timeout,
`ui/xui/main.cc:231-235`) → **Debug** menu → **Video** (`ui/xui/menubar.cc:254`).
`~` is the text monitor console, NOT FPS. The window persists across loadvm.
`FPS: N` = `g_nv2a_stats.increment_fps` (`ui/xui/debug.cc:388,405`) = frames
per 250 ms window (`profile.c:24-40`) counted at the guest's
`NV_PGRAPH_INCREMENT/READ_3D` flip increment (`pgraph.c:139`) — **the true
NV2A flip rate = true guest fps**, independent of host present rate.
`MSPF: N` = flip-increment→FLIP_STALL render ms (`debug.cc:421`,
`profile.c:44-47`). **MangoHud is unreliable on the PRIME/GLVND rig**: xemu
presents via GL on the iGPU while NV2A renders offscreen in Vulkan, so MangoHud
hooks a spurious GL context and reports garbage (1272 fps on the wrong GPU) —
runbook §instrument. Screenshot evidence: `~/lane4-grab.sh` (a direct
window-grab of a GL window is blank).

### 4. Guest-PC profiler

Sample point: `cpu_loop_exec_tb` on the vCPU thread at a TB boundary
(`accel/tcg/cpu-exec.c:920-921`) — race-free by construction. While armed,
`curr_cflags` forces `CF_NO_GOTO_TB|CF_NO_GOTO_PTR` (`cpu-exec.c:589`) so
chained hot loops/spins are sampled faithfully; this perturbs absolute speed —
the **ranking** is the deliverable. Never combine with -icount/record/replay.

Output: `XEMU_GUEST_PROFILE_PATH` is **rewritten (not appended) every
`DUMP_FLIPS` flips** — a rolling window, requested from the FLIP_STALL hook
(`pgraph.c:940`), flushed by the vCPU thread at its next sample; each flush
logs `[guest-profile] flush #N: X samples (of Y total, interval=U) -> path`
(`xemu-guest-profile.c:143-146`). Format: flat little-endian
`{uint32 eip, uint32 idx}` records, oldest→newest; `idx` = 1-based monotonic
index. Analyze:
`python3 tools/perf/guest_profile_symbolize.py <bin> --xbe default.xbe` → role
split (D3D8 / radar / game .text / kernel ≥0x80000000), ranked function
histogram, top EIPs, spin clusters + spin fraction (`tools/perf/README.md`).
**THE DILUTION TRAP** (runbook trap 6; tooling-gaps G4): a dump read after the
scene ended is dominated by post-scene idle — the campaign read "96% kernel
idle" from a really-86%-game-code scene. **`cp` the bin mid-scene**; distrust
end-of-run bins.

### 5–6. S2T dumps and surface logging

`s2t-miss:` line (`vk/texture.c:1561-1577`), ≤20 lines/5 s:
`s2t-miss: tex=<base8hex> <W>x<H> pitch=<P> fmt=<2hex> lin=<0|1> lvls=<L> | surf=<base8hex> <W>x<H> pitch=<P> swz=<0|1> color=<0|1> bpp=<B>`
(or `| surf=none-within`). Left of `|` = the texture bind that fell back;
right = the resident surface at/under that base. Read it to design the next
S2T extension against real shapes (it produced the 1800×1800 D16 map).
`surf-access:` (two lines per event, first 200 only, `vk/surface.c:814-827`):
`surf-access: <W|R> addr=<8hex> off=<6hex> len=<n> surf=<8hex> <W>x<H> <color|zeta> draw_dirty=<d> upload_pending=<u>` then
`surf-access:   bound_color=<0|1> bound_zeta=<0|1>`. Names WHICH access hits a
live surface. `surf-attr:` (every 30 flips, `vk/renderer.c:262-272`):
`surf-attr: <8hex> <W>x<H> <color|zeta> reads=<N> dl=<N>` — per-surface
read/download attribution. The counters reset **every frame** regardless, so
the line shows the sampled frame's counts, NOT a 30-frame sum.
`XEMU_SURF_DUMP` PPMs contain the just-downloaded RAM bytes — exactly what the
guest samples (`vk/surface.c:599-604`).

### 7–8. Vulkan validation, RenderDoc, panel dump

Validation plumbing: `display.vulkan.validation_layers` → `enable_validation`
(`vk/instance.c:205`); layer `VK_LAYER_KHRONOS_validation` + sync-validation
feature at instance create (`instance.c:207-229`); messenger (all severities ≥
verbose) prints `[vk] <message>` to stderr (`instance.c:45-58,241-254`). Armed
banner: `Warning: Validation layers enabled. Expect performance impact.`
(`instance.c:220-221`); failure banner `Warning: validation layers not
available`. **Status: BROKEN — as of 2026-07-27 no output even when set true;
open issue, never diagnosed** (`docs/perf/tooling-gaps.md` G1; suspects: xemu
plumbing vs PRIME/loader environment; bypass test documented there).
Armed-detector rule (G2, runbook trap 5): never quote "zero validation errors"
without the startup banner present in that same log.

RenderDoc: F10 = capture next frame (Shift 5 frames; Ctrl also toggles
`nv2a_pgraph_*` trace events for the span) — `ui/xui/menubar.cc:88-95`;
captures start/stop at the frame terminator (`vk/debug.c:43-70`,
`vk/renderer.c:296`), only while the RenderDoc UI is attached. Requires a
`CONFIG_RENDERDOC` build + loadable `librenderdoc.so` (`debug_renderdoc.c:41-81`;
the rig's `ring-launch.sh` sets LD_LIBRARY_PATH). `XEMU_VR_PANEL_DUMP`:
content-verification for the SBC world panel — full contract in spec A1; PPM
alpha-composited over the void color, rewritten on every content change
(`vr_sbc_panel.c:460-483`).

## State & data structures

**Phase timers** (`phase_timers.c:70-128`, private struct, nothing escapes the
TU): vCPU-thread append-only monotonic totals (`vcpu_blocked_ns`, `vcpu_dl_ns`,
`vcpu_dl_count`, atomically read); pfifo-private plain totals (`pfifo_idle_ns`,
`dl_proc_ns`, `present_ns`, `pf_leaf_ns[6]`, `pf_method_ns`, `pf_msub_ns[7]`,
`pf_psub_ns[3]`); per-window snapshots diffed at emit. No shared reset → no
torn-reset race; a wait straddling a window boundary smears ≤1 frame
(`phase_timers.h:76-92`). Clock = `QEMU_CLOCK_REALTIME` → `CLOCK_MONOTONIC`
(`phase_timers.c:146-149`). Leaf buckets are **depth-guarded**: only the
outermost leaf on the pfifo stack records, so a nested `pgraph_vk_finish` is
absorbed by its enclosing operation, never double-counted (`phase_timers.c:214-238`).

**NV2A counters**: `g_nv2a_stats` (`debug.h:254-263`) — `frame_working` (live,
memset at every FLIP_STALL, `profile.c:76`) + `frame_history[300]` ring +
`increment_fps`. Counters are **plain non-atomic ints** written from both vCPU
and pfifo threads; magnitude is the contract, not exactness (`debug.h:93-95`).
Single source of truth is the `NV2A_PROF_COUNTERS_XMAC` X-macro
(`debug.h:124-243`): adding an entry auto-generates the enum, the HUD/summary
label (enum minus `NV2A_PROF_`, `profile.c:88`), the per-frame reset, the
stderr summary, and the HUD row.

**Guest profiler**: fixed ring of 2^20 = 1,048,576 8-byte
`{uint32 eip, uint32 idx}` records (`xemu-guest-profile.c:80-89`), written and
read back **only by the vCPU thread**; the pfifo flip hook just sets an atomic
dump request (`94-98,175-184`). Dump = tmp + atomic rename, oldest→newest
(chronological, for spin-run detection) (`102-147`).

## Invariants (numbered; tagged)

- **I-1 [VERIFIED]** A zero from an unarmed detector is not a zero. Zero-because-clean and zero-because-off are indistinguishable in a log grep; the detector's arming banner must be present before any "zero X" claim. Burned twice: G1/G2 (`docs/perf/tooling-gaps.md`), runbook trap 5.
- **I-2 [VERIFIED]** An end-of-run guest-profile bin does not represent the scene: the ring is rolling and the file is rewritten every flush (`xemu-guest-profile.c:102-147`); only a mid-scene copy is scene-representative. Runbook trap 6 (the 96%-idle inversion).
- **I-3 [VERIFIED]** `increment_fps` is the true guest fps: it counts the guest's flip increment (`pgraph.c:139`), not host presents. Runbook §instrument equates it with true NV2A flip rate.
- **I-4 [VERIFIED]** Phase-line figures are 60-flip-window means; the `nv2a-prof:` line is one single frame's counters sampled every 60th frame (`phase_timers.c:512-549` vs `profile.c:58-74`). Cross-reading them as the same granularity is an error.
- **I-5 [DERIVED]** Phase buckets are disjoint sub-intervals of one thread's wall time: sum(pfifo leaves)+method ≤ pfifo_work and every residual ≥ 0 by construction (depth guard `phase_timers.c:226-238`, child subtraction `256-270,281-292`). A structurally negative residual would be clamped invisible (`clamp_nonneg`, `408-414`) — see F-7.
- **I-6 [VERIFIED]** A number from a static/menu/death screen is not a gameplay number (SB menus and death screens run 60 fps natively); only live, moving gameplay counts. Runbook traps 1–2.
- **I-7 [VERIFIED]** When the live overlay and a log-scrape disagree, the overlay wins. Runbook trap 3.
- **I-8 [VERIFIED]** mspf read within a few seconds of a gdb attach/detach is garbage (wall-clock desync). Runbook trap 4.
- **I-9 [VERIFIED]** Counter values are magnitudes, not exact counts: plain racy increments from two threads (`debug.h:93-95`); never build a claim on ±small differences.
- **I-10 [VERIFIED]** `S2T_MISS_*` tick only when the bind actually forced a download (`vk/texture.c:2304-2312`); zero misses alongside high SURF_TO_TEX means the copy path is working, not that no surface binds happened.
- **I-11 [VERIFIED]** `ZC_REJ_ZETA_BOUND` reads 0 forever — declared (`debug.h:240`) with no increment site in the tree (grep, 2026-07-27).
- **I-12 [DERIVED]** With all env gates unset, every instrument's hot path is a cached branch and behavior is upstream-identical (phase: `phase_timers.h:117-134`; profiler: `xemu-guest-profile.c:47-51`); the always-on residents (`nv2a-prof:` line, `g_nv2a_stats`) are the fork's accepted baseline cost.
- **I-13 [VERIFIED]** An armed guest profiler changes absolute timing (chaining forced off, `cpu-exec.c:589`); its numbers rank guest code but must never be compared against unprofiled frame times.
- **I-14 [VERIFIED]** Counters measure work, never meaning: a content bug (the shadow double-decode) can be numerically perfect on every instrument here. Visual verification is a separate obligation (G3).
- **I-15 [DERIVED]** A result measured on a binary older than the change is void (build-reality rule, `CLAUDE.md`): verify binary mtime before trusting any instrument output.

## Policies & decision points

- **Which instrument for which question**: where-does-the-frame-go → phase
  timers (`phase:` guest/pfifo/blocked split, `pfifo:` operation, `pfifo2:`
  draw-loop category). What-work-happened volume → nv2a-prof counters.
  Where-does-the-*guest*-spend → guest profiler (host overhead invisible by
  construction, `xemu-guest-profile.c:53-58`; use the Phase-0 TLB_*/TB_*/MMIO_*
  counters for that). Why-didn't-an-optimization-engage → reason counters
  (S2T_MISS/REJ, ZC_REJ) then the geometry dump. Pixel truth → RenderDoc /
  SURF_DUMP / PANEL_DUMP / screenshots.
- **Counter-attribution method** (normative, `02-lessons-learned.md` §1.9):
  every branch rejecting an optimization gets a reason counter; every surviving
  slow path gets a rate-limited geometry dump; never fix ahead of attribution.
- **`dl_stall` vs `download` vs `dl_proc`**: guest-side blocked wait vs
  pfifo-side copy leaf vs pending-queue processing — three views of one
  readback pipeline; triage compares them (kick latency vs copy cost).
- **Measurement runs use the standard ritual**: H-2 live-session check first
  (own command); cold boot or monitor-socket `loadvm` (never CLI `-loadvm`
  with the hotplugged SBC — trap 8); firmware pinned; scene = live gameplay.
- **Cost policy**: phase timers + counters are cheap enough for any run; the
  guest profiler distorts (dedicated runs only); validation (when fixed) and
  RenderDoc are heavy, correctness-only; SURF_ACCESS_LOG self-caps at 200
  lines precisely to keep steady-state timing clean.

## Failure modes

- **F-1** Unarmed detector quoted as clean — the campaign-falsifying failure
  (G1/G2); mitigation = I-1 ritual + planned `report-lib.sh` helper.
- **F-2** Guest-profile dilution (I-2): wrong CPU diagnosis for an hour.
- **F-3** `nv2a-prof:` silent tail truncation: the `n < 1536-48` guard drops
  counters late in the XMAC order on a busy line (`profile.c:63-68`); a dropped
  detector reads as zero. Re-verify headroom whenever counters are added.
- **F-4** `XEMU_SURF2TEX_DEBUG` armed without `XEMU_SURF2TEX_EXT`: the dump is
  called only inside the EXT-gated rejection block (`vk/texture.c:2315-2328`)
  — no output, which misreads as "no misses".
- **F-5** `XEMU_SURF_DUMP` writes to hardcoded `/home/pacarey/…`
  (`vk/surface.c:620-622`): elsewhere fopen fails silently, dumps vanish.
  Comment says ≤256² but code gates ≤512×512 (`616-617`) — trust the code.
- **F-6** `surf-attr` misread as a 30-frame sum — it is a 1-frame sample
  (reset every frame, `vk/renderer.c:261-271`).
- **F-7** Phase residual/`run` clamping (`phase_timers.c:408-414,461,490,501`) hides accounting bugs as 0.0; after any bucket rework check residuals stay plausibly small, not merely non-negative.
- **F-8** `present_gpuwait` silently changes meaning under `XEMU_ASYNC_PRESENT` (submit cost, not drain — `vk/renderer.c:213-232`); never compare it across runs with different async-present settings.
- **F-9** MangoHud on the rig reports the wrong GPU/context entirely (§3).
- **F-10** gdb attach corrupts several seconds of phase mspf (trap 4) and
  pauses the guest — never mid-firefight on a live session.

## Instrumentation (meta: how to verify an instrument itself is live)

Arming banners near startup: phase timers → `phase: XEMU_PHASE_TIMERS on …`
(`phase_timers.c:140`); guest profiler → `[guest-profile] ARMED: …`
(`xemu-guest-profile.c:217-220`) plus a `[guest-profile] flush #N: …` line each
flush; validation → `Warning: Validation layers enabled. Expect performance
impact.` (`instance.c:220`) — absence = detector off, whatever the config
says. Liveness cadences: the `phase:`/`pfifo:`/`pfifo2:` triple every 60
flips; `nv2a-prof:` every 60 frames (always); `surf-attr:` every 30 flips when
armed; `surf-access:` ≤200 lines total; `s2t-miss:` ≤20 per 5 s, EXT runs with
live fallbacks only. File instruments: guest-profile bin / `surf-dump-*.ppm` /
panel-PPM mtimes advance on events. RenderDoc: `nv2a_dbg_renderdoc_init`
errors to stderr if the library is missing (`debug_renderdoc.c:48-80`); a
missing capture with no error usually means no target-control (UI) connection.
Negative control: to prove a counter/bucket live, run a scene known to exercise
it (SURF_DOWNLOAD > 0 in the SB cockpit) before trusting its zero elsewhere —
I-1 applied to single counters.

## Verified-by

- `runbooks/rig-perf-measurement.md` traps 1–9: 1–2 (I-6), 3 (I-7), 4 (I-8,
  F-10), 5 (I-1, F-1), 6 (I-2, F-2), 7 (log-attribution when reading build
  logs), 8 (ritual policy), 9 (build/verify discipline, I-15).
- `docs/perf/tooling-gaps.md` G1 (validation broken), G2 (armed-detector
  automation), G3 (I-14), G4 (profiler dump points, F-2).
- `docs/perf/loc/02-lessons-learned.md` §1.9 (counter-attribution method; the
  S2T/ZC counter set's campaign payoff).
- Code walk 2026-07-27 (this spec): all file:line citations above; grep
  confirming `ZC_REJ_ZETA_BOUND` has no increment site (I-11). Live-rig
  overlay/MangoHud/phase-mspf behavior: the 2026-07 SB campaign record
  (`docs/sb-graphics-findings.md`, runbook §instrument).

## Confidence & unknowns

- **G1 root cause unknown** — is validation dead in xemu's plumbing (the path
  `instance.c:205-254` *looks* correct: layers + messenger + stderr callback)
  or in the rig's loader/PRIME environment? Until diagnosed, every VK
  correctness claim rests on asserts and eyes only. [open issue]
- **Phase-timer accounting under the full overlap/async stack** is [DERIVED]:
  disjointness is structural, but no deliberate cross-check (buckets vs an
  external clock, all levers on) has run; F-8 shows one bucket's semantics
  already shift with a lever.
- **Header-vs-code drift**: `phase_timers.h:31-38`'s example `phase:` line
  omits `dl_proc`; `XEMU_SURF_DUMP`'s comment says ≤256² vs code ≤512². Code
  is authoritative; comments not yet fixed.
- **Overlay legend edge case** [DERIVED]: `nv2a_profile_get_counter_value`
  reads `frame_ptr-1` with unsigned wraparound at startup (`profile.c:94-96`);
  first-frames legend values may read a stale slot. Cosmetic.
- **`XEMU_GUEST_PROFILE=1`** (every TB) is legal but untested; expect severe
  slowdown. Non-numeric values arm with the 4096 default (`atoi` → 0 → default
  kept). GL-backend counter parity is unaudited; this spec's sites are the VK
  renderer's (H-3 forces VULKAN).
