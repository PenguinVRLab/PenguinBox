# Spec — Per-game hacks framework

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| A | `hw/xbox/xbox-game-hacks.{c,h}`; callers: `hw/xbox/nv2a/pgraph/vk/renderer.c:245`, `target/i386/tcg/system/bpt_helper.c:222`; build: `hw/xbox/meson.build:15` | v1 draft | 2026-07-27 |

## Purpose & scope

The single sanctioned home for **title-gated runtime divergences from stock
emulation** — behavior that changes depending on which game is running.
Framework rule: every such divergence lives here and is enumerated in this
spec's inventory table. **An unlisted hack is a future debugging landmine**;
this file is the anti-landmine registry.

Out of scope (behavior that varies per game but is not a guest-code hook):
per-title config toml practice (Tier B7), title-agnostic perf env levers
(`XEMU_SURF2TEX_*`, `XEMU_TLB_FASTPATH`, overlap stack — spec A2), and VR
per-game camera profiles (spec A1). The framework contains **exactly one
hack** as of 2026-07-27.

## External interface

- `xbox_game_hacks_frame_tick(void)` — once per frame at the NV2A flip site,
  pfifo thread (`vk/renderer.c:245`, after the predl/narrowfence/
  async-present toggle refreshes). Re-reads cadence, decides scan-vs-skip
  for the upcoming frame, schedules arm/disarm.
- `xbox_game_hacks_maybe_skip_scanner(CPUState*)` — first thing in the i386
  `breakpoint_handler` (`bpt_helper.c:222`), vCPU thread, inside
  `#ifdef XBOX`. For our EIPs it resumes the guest via
  `cpu_loop_exit_noexc()` and **does not return**; for any other EIP or a
  watchpoint hit it is a no-op passthrough to the normal handler.
- **Env**: `XEMU_RADAR_SKIP_N` — initial/default cadence N, read **once**
  (latched at first tick; changing the env mid-run does nothing).
  `XEMU_RADAR_SKIP_FILE` — watch-file path (default `/tmp/radar-skip-n`),
  re-read **every frame** and live-overriding N while present. N<2 = OFF.
  No config key.
- **stderr**: `radar-skip: N=%d -> ON/OFF` on every cadence change;
  `Steel Battalion detected; radar-scan breakpoints armed`;
  `loaded title is not Steel Battalion (0x43430002); disabled`.
- **Counter**: `NV2A_PROF_RADAR_SCAN_SKIP` (`nv2a/debug.h:189`), one bump
  per scanner ret-emulated.

## State & data structures

- `g_scanner_eip[4] = {0x25d40, 0x26460, 0x26b80, 0x27250}` — fixed XBE
  vaddrs of the four radar scan-AND-draw routine entries.
- Cross-thread atomics: `g_skip_this_frame` (pfifo writes, trap handler
  reads; racy-by-a-frame is acceptable), `g_disabled` (latched once the
  title is known not-SB).
- pfifo-only: `g_want_installed` (last scheduled arm intent). vCPU-only:
  `g_title_state` (-1 unknown / 0 not-SB / 1 SB).
- **Deliberately no cached `CPUBreakpoint*`** — x86 reset and vmstate load
  both `cpu_breakpoint_remove_all(cs, BP_CPU)` and free the objects, so the
  install/remove callbacks are idempotent and address-keyed instead.

## THE HACK INVENTORY (complete as of 2026-07-27)

| # | Hack | Game match | What it changes | Why it exists | Gate |
|---|---|---|---|---|---|
| GH-1 | Steel Battalion **radar-scan temporal downsampling** (perf Tier-1) | XBE **cert title-id == 0x43430002**, probed directly from guest memory (XBE header magic at 0x10000 → cert ptr at +0x118 → title-id at +0x8). Not a hash, not a name match. | On N-1 of every N frames, host-side **BP_CPU breakpoints** at the four scanner entry EIPs emulate each routine's `ret` at entry (`eip = *(u32*)esp; esp += 4`), so the routine body never executes. Scan frames (`frame_count % N == 0`) run the scanners fully un-instrumented (breakpoints removed). Guest bytes are **never patched**. | The four near-identical routines walk the 128x128 GPU-rendered radar buffer byte-by-byte and emit D3D blip draws once per frame each — a dominant chunk of the ~93%-busy TCG guest thread. Skipping refreshes blip data at 30/N Hz while the guest work disappears. | **Default OFF.** ON iff title matches AND N≥2 via `XEMU_RADAR_SKIP_N` (initial, latched) / watch file `/tmp/radar-skip-n` (live). Wrong title latches OFF for the session. |

There is no second row. Callers grep (`xbox_game_hacks_`) hits exactly the
two sites above; the title-id constant `0x43430002` appears nowhere else in
the tree (see Verified-by). Any new hack MUST add a row here in the same PR
(spec-with-change rule, `00-specification-plan.md` §5).

## Invariants

1. [VERIFIED] **OFF == stock**: N<2, or a non-SB title, means zero
   breakpoints exist and behavior is byte-identical to unhacked xemu.
   (2026-07-26 six-agent pre-flight, all green; `runbooks/radar-skip-test.md`.)
2. [VERIFIED] **Title gating**: hooks arm only when the resident XBE cert
   title-id is 0x43430002; any other resident title latches `g_disabled`
   with one stderr line and never re-arms that session.
3. [VERIFIED] **Savestate-clean**: breakpoints are host-only (BP_CPU) and
   never enter a savestate; loadvm wipes them and the feature silently
   re-arms within ≤N frames (counter reappears; no log line — verified live).
4. [VERIFIED] **ret-emulation is exact** for these four routines: each ends
   in `ret` (0xC3, not `ret imm16`), takes zero arguments, preserves
   ebx/esi/edi/ebp, and callers ignore eax — static disassembly of
   `default.xbe` (title 0x43430002).
5. [DERIVED] **Breakpoints exist only on skip frames.** A live BP_CPU
   de-optimises its whole 4 KiB code page (CF_BP_PAGE single-step); arming
   only on skip frames keeps scan frames fully optimised.
6. [DERIVED] **Arm/disarm needs no TB flush**: every scanner call is a
   cross-page `call` (dispatcher 0x45xxx → 0x25-27xxx) and i386 goto_tb
   chaining is same-page-only, so control always routes through
   `check_for_breakpoints()`. If this analysis were ever wrong, the failure
   is benign — the skip simply doesn't happen; never a crash.
7. [DERIVED] **Non-interference**: the trap hook returns immediately for
   watchpoint hits and foreign EIPs, so gdbstub and guest debug-register
   breakpoints are unaffected (BP_CPU, not BP_GDB, and our handler runs
   before guest-#DB injection).
8. [DERIVED] **Cadence-transition race is safe**: a hit on a scan frame sets
   `EFLAGS.RF` to suppress our own breakpoint for exactly one instruction so
   the scanner runs normally.
9. [DERIVED] **Threading discipline**: cadence decided on the pfifo thread,
   published by atomic; breakpoint-list mutation only via
   `async_safe_run_on_cpu()` (exclusive vCPU context, the
   `mem_access_callback_insert()` pattern).
10. [VERIFIED] **Inventory completeness** (the point of this spec): API call
    sites and title-id constant enumerated by grep, 2026-07-27 — see
    Verified-by for the runnable commands.

## Policies & decision points

- **Breakpoints, not guest byte-patching** [DERIVED, consequence VERIFIED]:
  patching would contaminate savestates and churn TB invalidation;
  host-only breakpoints keep loadvm clean (verified live, invariant 3).
- **Toggle-armed, not always-armed** [DERIVED]: avoids single-stepping the
  scanner's own hot loop on scan frames (multi-ms periodic stutter).
- **Per-frame recompute** (`frame_count % N`) [DERIVED]: no persistent
  cadence state → loadvm mid-session is automatically safe.
- **Title probe reads guest memory directly**, not `xemu_get_xbe_info()`
  [DERIVED]: avoids that helper's shared-static reentrancy.
- **Probe latches once** [VERIFIED operationally]: -1 (XBE not resident)
  retries on the next transition; 0 (wrong title) latches OFF for the
  session. Consequence: **set N only while SB is running** — arming during
  the dashboard latches the feature off until process restart
  (`runbooks/radar-skip-test.md`).
- **Fail-safe on unreadable return address** [DERIVED]: run the scanner
  (set RF) rather than corrupt EIP.

## Failure modes

- **Armed on the wrong resident XBE** (dashboard boot with N preset) →
  silently latched OFF all session; operator trap, documented in the runbook.
- **Stale `/tmp/radar-skip-n`** from a previous session re-arms silently on
  launch — the runbook's first step is `rm` of the watch files.
- **Cosmetic residual (known, bounded)**: a texture-filter field inherited
  by two small HUD icon overlays on skip frames — proven bounded by
  disassembly; no gameplay state touched. v2 option: hook past the scan loop
  instead of the entry.
- **Counter misread**: `RADAR_SCAN_SKIP` counts scanners skipped per skip
  frame (K), not a cadence average — K=2 in scenes where guest flag
  `[0x356ae0]==0` because the game itself gates scanners 3/4 on that flag.
- **EIPs are build-specific** [ASSUMED risk]: the four vaddrs were verified
  against this `default.xbe`; a different SB revision/region with the same
  title-id would mis-target (see Unknowns).
- Invariant-6 wrongness degrades to no-op, never crash (by design).

## Instrumentation

- Arm state = the stderr lines (`N=x -> ON/OFF`, `detected/armed`,
  `disabled`), **not** the counter value; after loadvm, re-arm is silent and
  the returning counter is the signal.
- `NV2A_PROF_RADAR_SCAN_SKIP` in the nv2a profile counters.
- Live A/B: `echo N > /tmp/radar-skip-n` / `rm` the file; cadence-change
  line confirms registration within a frame.

## Verified-by

- `runbooks/radar-skip-test.md` — end-to-end live procedure verified
  2026-07-26: arm lines, counter, disarm, loadvm re-arm, zero crashes;
  six-agent pre-flight all green. Design + verification also in
  `docs/sb-graphics-findings.md` §8.5 and the ISS-B16 file.
- Static disassembly of `default.xbe` for invariant 4 and the K=2 gating
  flag (confirmed three ways: disassembly, counter, live monitor peek
  `x /1wx 0x356ae0`).
- Inventory greps (runnable; run 2026-07-27):
  - `grep -rn "xbox_game_hacks_" --include="*.c" --include="*.cc"` →
    exactly `renderer.c:245` + `bpt_helper.c:222` (+ the framework files).
  - `grep -rln "0x43430002" --include="*.[ch]" --include="*.cc"` → only
    `hw/xbox/xbox-game-hacks.{c,h}`.

## Confidence & unknowns

- Perf yield in busy-radar combat (~23K reads/frame scaling) never isolated
  from the rest of the stack; idle-cockpit FPS is unchanged (guest-CPU-bound
  there) with render thread ~7pp lighter. [DERIVED expectations, runbook]
- Whether other SB revisions/regions share these EIPs — unknown; the
  title-id gate probably also matches them. [ASSUMED landmine, flagged]
- The per-frame `fopen` of the watch file on the pfifo thread is assumed
  negligible; never measured. [ASSUMED]
- Whether `frame_count` (flip-count) parity vs guest logic frames can drift
  under frame-overlap levers — believed irrelevant (any N-of-M pattern
  still skips), untested. [ASSUMED]
- The framework has one consumer; its "framework-ness" (naming, the two hook
  points) is untested against a second hack with a different shape (e.g. a
  config-gated patch) — expect interface growth then.
