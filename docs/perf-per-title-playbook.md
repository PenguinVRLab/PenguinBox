# Per-title performance playbook (what the SB campaign generalizes to)

The SB work produced three reusable assets and one decisive question. Steel
Battalion itself ended capped (fixed-30 engine), but **most of what we built is
title-agnostic and pays off bigger on other games** — SB was close to the worst
case (query-spam + logic-driven readback + a hard 30-lock). Use this before
sinking effort into any new title's frame rate.

## What generalizes (platform assets, not game hacks)

1. **The CPU/GPU overlap stack** (`docs/vr/present-wall-plan.md`, branch
   `sb-graphics-research`) — a *renderer-wide* improvement: async present +
   async report delivery + in-batch uploads + renderpass-churn kill + per-slot
   resource recycling. It removes the serial "record → wait GPU → flip" wall for
   ANY title on the VK NV2A path. On SB the win is masked by its 30-lock; **on a
   title that isn't fixed-30 (or is time-based) the same stack yields real higher
   fps or real headroom.** All stages default-OFF, env-gated, per-lever.
2. **The frame-limiter RE method** (`runbooks/rig-perf-measurement.md` §RE) —
   profiler → XBE static RE (capstone + thunk-table deobfuscation) → gdb
   single-hit backtrace → RAM diff. Cracks any title's present/interval wait.
3. **The measurement discipline** (`runbooks/rig-perf-measurement.md` §traps) —
   the FPS instrument (xemu Video Debug, not MangoHud) and the four traps
   (dead-screen-60, near-death-save contamination, log-scrape-vs-live,
   gdb-perturbs-mspf). These caused two wrong calls on SB; they'll bite on any
   title.

## The per-title triage (run in order; stop when you have the answer)

1. **Bound?** Guest-PC profiler (`XEMU_GUEST_PROFILE=N`). Kernel-idle spin
   dominating ⇒ blocked on the emulated GPU/vblank (pfifo/present-bound → the
   overlap stack applies). Real game code dominating ⇒ guest-CPU-bound (different
   problem: KVM candidacy, guest idle-skip, or HLE — measured per title, never
   assumed).
2. **What's the limiter?** Find the present/vblank wait (the RE method). Is fps
   already at the display cap, or is the *game* self-limiting?
3. **THE DECISIVE QUESTION — timestep coupling.** Before "unlocking" any fps cap,
   determine how the game advances logic (grep the timestep for
   KeQueryPerformanceCounter / KeTickCount-delta vs a fixed per-frame constant;
   look for +1-per-present frame counters):
   - **Time-based** (QPC / tick-delta / vblank-delta) → a present-interval unlock
     is a **FREE higher-fps win**. Ship it (after gates).
   - **Fixed-timestep** (one sim step per present, hardcoded dt, frame-count
     timers — SB) → a present unlock runs the game at **N× speed**. Higher fps
     requires **sim/render decoupling (frame generation)**, a real engine feature,
     NOT a byte-patch. Do not ship a naive unlock.
4. **Correctness gates before default-ON** (every title): armed sync-validation,
   `flicker_detect.py` A/B, any game-logic readback byte-diff (SB's radar was the
   canary — occlusion/readback results a game *consumes* must be correct, never
   stale), then owner eyes. Ship default-OFF until all pass.

### 1b. The surface-traffic triage (added after the LoC campaign, 2026-07-27)

If step 1 says pfifo-bound, decompose `pfifo:` further **before** reaching for
the overlap stack — LoC's walls were entirely different from SB's, and the
same buckets name them:

- **`download` dominating** → surface traffic, not present. Sub-triage with
  the always-on `S2T_MISS_*` counters (which class of surface-as-texture bind
  is falling back) and `XEMU_SURF2TEX_DEBUG=1` geometry dumps (the actual
  tex/surface shapes). Known classes and their shipped levers, all opt-in:
  - reinterpretation binds (swizzled tex over linear surface, offsets,
    tiles) → `XEMU_SURF2TEX_EXT` (Morton-bounce / quadrant / gather);
  - eviction cascades from **memory aliasing** (game reuses one region — even
    one base address — for multiple surface shapes; watch
    `SURF_CREATE`/downloads ping-ponging) → `XEMU_ALIAS_SURFACES`;
  - **feedback rendering** (sampling the still-bound target: cockpit-monitor
    color feedback, and *soft-particle smoke sampling the bound depth* —
    the visual tell is slowdown tracking smoke/haze/grain/post effects, NOT
    unit count) → `XEMU_SURF2TEX_ZEROCOPY` + `XEMU_SURF2TEX_FEEDBACK`.
- **`SURF_TO_TEX` (copy count) ≈ renderpass count ≈ mspf, per-frame** → the
  copies themselves are the wall; check whether the consumer needs them
  per-*draw* or per-*frame* (LoC's depth-as-texture needed one per frame;
  the per-draw key was doing 700×).
- **Depth-format caution**: any lever touching depth-as-texture must respect
  the *decode chain* — Xbox `…DEPTH_Y16_FLOAT` textures expect raw bits that
  the shader decodes; sampler-normalized depth views double-decode and corrupt
  shadow comparisons. Bit-identical storage ≠ identical semantics.

Method note that made this tractable: **put a reason-counter on every branch
that rejects an optimization, and a rate-limited geometry dump on every
surviving slow path** — each iteration then names its successor instead of
guessing. And keep a **savevm of the worst scene** (loaded via monitor, not
CLI — hotplugged devices break `-loadvm`): it turns every A/B into a
90-second deterministic run.

## Outcome matrix

| title profile | present-unlock verdict | best lever |
|---|---|---|
| pfifo-bound + time-based timestep | free higher fps | overlap stack, then unlock the cap |
| pfifo-bound + fixed-timestep (SB) | 2× speed — do NOT | overlap stack for headroom/stability; frame-gen for true higher fps |
| pfifo-bound, `download`-dominated (LoC) | n/a until surface traffic fixed | S2T levers (EXT/ALIAS/ZEROCOPY/FEEDBACK), *then* re-triage |
| guest-CPU-bound | present work irrelevant | KVM / idle-skip / HLE (per-title) |

## Tooling worth building (catalog-level ROI, not per-game)

Each of these would have saved days on SB and pays back across every title:
- **Trustworthy always-on frametime overlay in-emulator** (MangoHud is unreliable
  on the PRIME/GL display path; lacking a reliable counter caused the
  dead-screen-60 misread).
- **Kernel-thread-aware guest debugger + stack-sampling profiler** — trace the
  *blocked* game thread's present chain in one step instead of a multi-lane hunt;
  no hot-breakpoint deadlocks.
- **Scene-tagged save states + state-stamped perf samples** (record the game
  state word with each sample) — auto-flags "this is the dead screen / a
  near-death outlier" and prevents contaminated averages.
- **An "uncap present but pin sim-rate" experiment toggle** — answers the
  timestep-coupling question empirically in seconds instead of by static RE.
- **A reusable disassembler workspace** (xref/thunk DB) instead of ad-hoc
  capstone scripts per lane.

## The one-line takeaway

The overlap stack + the timestep triage are the durable output: **for each new
title, one cheap RE check (time-based vs fixed-timestep) tells you whether fps is
free, needs frame-gen, or is the wrong target — before you spend a day on it.**
