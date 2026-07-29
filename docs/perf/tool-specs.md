# Tool specifications — the perf/correctness toolkit

Implementation-ready specs for the tools identified in
[`tooling-gaps.md`](tooling-gaps.md) (T1–T9 map to gaps G1–G10) plus three new
tools scoped after the campaign post-mortem (T10–T12). Written 2026-07-27.
Each spec: purpose → interface → behavior → implementation notes → acceptance
criteria → size. Ordered by build priority, not gap number.

Conventions used throughout:
- **Rig scripts** live in `~/` on the rig, are self-contained bash, `set -u`,
  write results to `<name>.result` ending with a `<NAME>-DONE` sentinel line,
  and never depend on the invoking ssh session staying alive (`setsid`,
  output to files).
- **Repo tools** live in `tools/perf/` and are committed.
- **Exit codes**: 0 = pass/clear, 1 = fail/found, 2 = tool could not run
  (missing dep, detector unarmed, etc.). Callers must distinguish 1 from 2.

---

## T1. Validation-layer plumbing fix + armed-banner contract  [G1 — DO FIRST]

**Purpose.** Make Vulkan validation actually work in this fork, and make its
*on/off state* machine-checkable, so "0 validation errors" can never again be
an empty grep.

**This is an investigation + small fix, not a new tool.** Spec of the
procedure:

1. **Trace the plumbing** in `hw/xbox/nv2a/pgraph/vk/instance.c`:
   - Does `g_config.display.vulkan.validation_layers` reach
     `VkInstanceCreateInfo.ppEnabledLayerNames`? (Lines ~29–80 do a
     presence check against `vkEnumerateInstanceLayerProperties`; confirm the
     result is actually *used* when `enable_validation` is true, and that the
     check isn't silently failing on the rig.)
   - Is a `VkDebugUtilsMessengerEXT` created, with which severities, and does
     its callback write to stderr? Suspects: messenger created before the
     instance-level function pointers are loaded via volk; severity mask
     excluding ERROR; callback routed to a tracepoint nobody enables.
2. **Environment bypass test** (decides which side the bug is on):
   `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LOADER_DEBUG=all`
   in a launcher. Messages appear → xemu plumbing bug (fix per step 1).
   Nothing appears → loader/environment problem (check `VK_LAYER_PATH`,
   32/64-bit layer mismatch, and whether the PRIME/GLVND setup interferes) —
   document the workaround in the runbook.
3. **The armed-banner contract** (the durable deliverable): on successful
   validation enablement, xemu must print exactly one line to stderr:
   `vk: VALIDATION ARMED (VK_LAYER_KHRONOS_validation)`
   — unconditionally greppable, stable format, emitted only when the layer is
   confirmed present in the created instance. All reporting tools key off
   this line (see T2). If validation was requested but could not be armed,
   print `vk: VALIDATION REQUESTED BUT NOT ARMED` and continue.

**Acceptance.** (a) A deliberately-broken barrier (temporarily remove one
transition) produces validation errors in the log on the rig. (b) The banner
appears exactly when the layer is active. (c) With validation off, the banner
is absent and T2's `vk_error_count` refuses to answer.

**Size.** Half a day including the deliberate-bug test.

---

## T2. `report-lib.sh` — armed-detector reporting primitives  [G2]

**Purpose.** One shared library so every report script inherits the
"detector must be armed before its zero is believed" rule.

**Interface.** Sourced (`. ~/report-lib.sh`), providing:

```
vk_error_count LOGFILE
    stdout: integer count of validation messages
    exit 0 if banner found (count valid, may be 0)
    exit 2 + stdout "DETECTOR-OFF" if the armed banner is absent
detector_armed LOGFILE BANNER_REGEX
    generic form; exit 0/2 as above
counter_sum LOGFILE PREFIX_REGEX
    sums `NAME=VALUE` tokens across all nv2a-prof lines (the awk idiom used
    ~30 times this campaign, frozen in one place)
mspf_dist LOGFILE
    stdout: "windows=N min=A med=B p90=C max=D" from phase: lines
frame_table LOGFILE FIELD[,FIELD…] [MIN_MSPF]
    per-frame correlation table (mspf + named counters), the falsifier tool
```

**Behavior details.** `vk_error_count` greps
`Validation (Error|Warning)|VUID-` only after `detector_armed` passes with
the T1 banner. `counter_sum`/`frame_table` must tolerate counters that only
print when nonzero (absent token = 0).

**Acceptance.** Unit-style: run against three fixture logs (armed+errors,
armed+clean, unarmed) → counts / 0 / DETECTOR-OFF with exit 2.

**Size.** ~1 hour. Then retrofit `loc-s2t-report.sh`, `sb-regression-check.sh`
and the bench harness (T3) to source it.

---

## T3. `perf-bench.sh` — the unified measurement harness  [G7]

**Purpose.** Replace the ~10 hand-cloned launcher/watcher/reporter scripts
with one parameterized tool. This is the workhorse; almost every other spec
plugs into it.

**Interface.**

```
perf-bench.sh --out NAME [options]
  --flags "A=1 B=1"       env toggles for this leg (appended to the base stack)
  --base-stack|--no-base-stack
                          include the standard perf stack (TLB_FASTPATH,
                          PHASE_TIMERS, FRAME_OVERLAP, ASYNC_PRESENT,
                          ASYNC_REPORTS, INBATCH_UPLOADS, BATCH_QRESET,
                          PIPE_OPT, ELEMENT_BATCH). Default: on.
  --title loc|sb          picks ISO + config from the per-title profile (T8);
                          default loc
  --snapshot TAG          monitor-loadvm TAG after boot (default: none = cold)
  --window SECONDS        measurement window after scene-settle (default 90)
  --wait-heavy [REGEX]    delay the window until a heavy-scene signature
                          appears in the log (default regex:
                          PIPELINE_RENDERPASSES=[1-9][0-9]{2,}); timeout 780 s
                          → exit 2
  --screenshot            lane4-grab at window end → NAME.png
  --keep|--kill           leave the emulator running (default) or kill at end
  --counters "REGEX"      extra counter families for the totals block
```

**Behavior.**
1. H-2 via T7 (`h2-check.sh`); a *foreign* live session → exit 2 immediately
   (a session this tool itself started earlier is fair game to recycle).
2. Launch detached (setsid, per-leg log `NAME.log`), monitor-loadvm if
   requested (sleep ≥18 s first — hotplug; runbook trap 8).
3. Optional `--wait-heavy`, then the window.
4. Emit `NAME.result` with **exactly four blocks** (the format every verdict
   this campaign was read from, frozen):
   - `== gates ==` — the flag banner lines found in the log (proof of what
     was actually on);
   - `== totals ==` — `counter_sum` over `^S2T_|^ZC_|^SURF_|^ALIAS_|^TEX_`
     plus `--counters`;
   - `== frames ==` — `frame_table mspf,SURF_TO_TEX,PIPELINE_RENDERPASSES`
     (worst 10 by mspf);
   - `== dist ==` — `mspf_dist`, then `vk_error_count` (which may print
     DETECTOR-OFF), then `alive: yes|no`.
   Terminate with `BENCH-DONE`.
5. A/B mode is just two invocations; a thin `perf-ab.sh A_FLAGS B_FLAGS`
   wrapper runs both legs and prints the two `.result` files side by side.

**Implementation notes.** Absorb the monitor-loadvm idiom, the drop-proof
sentinel pattern, and the settle timings from the retired `loc-bench-*.sh`
family — then delete them (keep names as one-line wrappers for a week).

**Acceptance.** Reproduce the v9.4 verdict numbers (copies 1–2, rpass 20–23,
med ≈50) from a single `perf-bench.sh --flags "XEMU_SURF2TEX_EXT=1
XEMU_ALIAS_SURFACES=1 XEMU_SURF2TEX_ZEROCOPY=1 XEMU_SURF2TEX_FEEDBACK=1"
--snapshot vm-20260727054737 --out v94-check` invocation.

**Size.** Half a day.

---

## T4. `visual-gate.sh` — golden-frame regression gate  [G3]

**Purpose.** Catch content bugs (the shadow class) mechanically. Counters
measure work; this measures pixels.

**Interface.**

```
visual-gate.sh --golden-set NAME [--update] [--flags "…"] [--title loc|sb]
               [--threshold DSSIM]
```

A golden set is a directory `goldens/NAME/` containing one or more
`(snapshot-tag, settle-seconds)` capture points listed in `manifest.tsv`,
with a stored PNG per point.

**Behavior.**
- For each manifest row: boot via T3 machinery (`--snapshot`, settle,
  `lane4-grab`), then compare capture vs golden:
  `compare -metric DSSIM capture.png golden.png diff.png`.
- Pass if DSSIM ≤ threshold (default **0.05** — deliberately loose: the gate
  exists to catch *broken shadows*, not dithering or HUD timers; tune from
  the first week of data).
- `--update` refreshes goldens **only** with `--i-reviewed-these` also given —
  a golden refresh is a human act, and the flag name should make the reviewer
  claim explicit.
- Output: per-point PASS/FAIL + DSSIM, diff images kept on FAIL, exit 1 on
  any FAIL, `VISUAL-DONE` sentinel.

**Implementation notes.** Capture determinism: same snapshot + fixed settle →
LoC's attract is deterministic enough per the campaign's repeated runs; if a
point proves noisy, capture 3 frames 1 s apart and take the min-DSSIM match.
Mask the Video Debug overlay region if enabled (or capture with the overlay
closed — preferred).

**Acceptance.** The v9.3 shadow bug, replayed (check out the broken commit),
must FAIL against v9.4 goldens on a shadow-scene point; v9.4 must PASS.
That's the regression test *for the gate itself* and is cheap to run once.

**Size.** Half a day + a goldens-review session with the owner.

---

## T5. Strict-compiler gate  [G5]

**Purpose.** Never again merge code the modern toolchain rejects.

**Two parts:**

1. **Rig cflags** (immediate): add
   `-Werror=implicit-function-declaration -Werror=implicit-int` to the rig
   build. One line in the build invocation; makes GCC 13 reject what GCC 16
   rejects, for this class.
2. **`tools/perf/cross-syntax-check.sh`** (pre-commit, seconds):
   ```
   cross-syntax-check.sh [FILE…]        # default: git diff --name-only HEAD
   ```
   For each touched `.c` under `hw/xbox/`, run inside the `xemu-win64-cross`
   image: `x86_64-w64-mingw32.static-gcc -fsyntax-only -Werror=implicit-function-declaration
   @includes.rsp FILE`. Generate `includes.rsp` once per build from
   `build/compile_commands.json` (extract `-I/-D` for a sibling file; a full
   command reuse is fine too). Exit 1 on any failure, listing file:line.
   *Scope honestly:* syntax-only with borrowed flags is an approximation —
   it catches the implicit-decl/undeclared-symbol class (the class that bit
   us), not link errors. The full cross build stays the release gate.

**Acceptance.** Checkout of the pre-fix commit (missing
`pgraph_vk_surface_feedback_flush` prototype) → part 2 fails in <10 s.

**Size.** (1) minutes; (2) ~1 hour.

---

## T6. Guest-profiler upgrades  [G4]

**Purpose.** Make the guest-PC profiler trustworthy at scene granularity.

**Changes to `accel/tcg/xemu-guest-profile.c`:**
1. **Dump-on-demand**: a monitor-reachable trigger. Cheapest robust form:
   watch a sentinel file (`XEMU_GUEST_PROFILE_TRIGGER=/tmp/gp-dump` — flip
   the same atomic the flip-thread flush uses when the file appears, then
   unlink). Avoids threading a new HMP command through QEMU plumbing.
2. **Windowed dumps**: `XEMU_GUEST_PROFILE_SEGMENTS=1` writes
   `PATH.<flushseq>` instead of overwriting `PATH`, keeping the last K=8
   segments (unlink older). End-of-run dilution (runbook trap 6) becomes
   recoverable after the fact.
3. **Header doc**: state loudly that the ring is rolling and end-of-run bins
   are diluted.

**New repo tool `tools/perf/guest_profile_histo.py`** (freeze the inline
one-liner used twice):
```
guest_profile_histo.py BIN [--buckets 16|4096] [--top N] [--split-kernel]
```
Prints: valid-sample count, kernel/game split (PC ≥ 0x80000000 = kernel),
top-N hot buckets and hot pages with percentages. Input format:
`GpSample { uint32 eip; uint32 idx; }`, idx==0 = empty slot.

**Acceptance.** Mid-scene trigger during the LoC snapshot scene reproduces
the 86%-game-code split (not the diluted 96%-idle).

**Size.** 2–3 hours total.

---

## T7. `h2-check.sh` — the live-session check as a tool  [G8]

**Purpose.** Make the H-2 hard rule un-mistypeable.

**Interface.** `h2-check.sh` (no args). Rig-local; callers over ssh run
*only* this script in the invocation (that's the whole point — no sibling
commands to ghost-match; runbook trap 8).

**Behavior.** Exactly:
```
pgrep -x pcsx2-qt
pgrep -x mupen64plus
pgrep -f "[q]emu-system-i386"
```
Prints one line per emulator: `NAME: none` or `NAME: PID [comm/args first
60 chars]`. Exit 0 = all clear, 1 = something live. No other output, no
compound, no cleverness.

**Acceptance.** Run while a `strings …/qemu-system-i386` executes in another
shell → still exits 0 (the ghost-match scenario, now structurally impossible
to self-inflict since the script contains no such sibling).

**Size.** 15 minutes. T3 calls it.

---

## T8. Per-title profiles  [G10]

**Purpose.** Stop `sb-floor-s1.toml` silently serving two games; carry
"what's verified on this title" next to the settings.

**Layout.**
```
configs/
  sb.toml     sb.env     # verbatim current sb-floor-s1 + the SB-verified stack
  loc.toml    loc.env    # LoC: full SBC input map, validation on once T1 lands
```
`*.env` = `KEY=VALUE` lines (the flag stack for that title) with a mandatory
header comment: what is verified on this title, by whom, when — e.g. loc.env
carries the five S2T flags with "perf verified on snapshot vm-20260727054737;
visuals owner-verified 2026-07-27"; sb.env carries none of them with "S2T
flags engaged-and-benign per counters but NOT eye-verified on SB — leave off."

**LoC input map** (the campaign's dead-end): a full
`[input.keyboard_sbc_scancode_map]`. Correction from the B6 spec dig
(docs/internal/B6-input-stack.md P3): the "scancode-18 collision" was
mischaracterized — the gamepad and SBC keyboard maps are **driver-selected,
never merged**, so `rtrigger=18` (gamepad map) and `oxygen_supply_system=18`
(SBC map) can never both fire; 18 is `o`, not `w` (`config_spec.yml:139`'s
comment is wrong). The real traps to fix here instead: (a) **main weapon /
sub weapon / lock-on are mouse-hardwired** — no keyboard scancode exists for
them, so "PRESS MAIN WEAPON BUTTON" needs a mouse click in the viewport;
(b) a typo'd/foreign `portN_driver` silently falls back to Duke, flipping
every SBC key to its gamepad meaning with no message — assert the driver in
the profile header; (c) bind the remaining combat controls (`sight_change`
et al.) and document the layout in the file. Owner session required to
sanity-test in-game.

**Consumers.** T3's `--title` flag; the runbook's launch section points here.

**Size.** ~1 hour + owner input-testing session.

---

## T9. `rig-sync.sh` — verified two-way sync  [G9]

**Purpose.** Replace the scp-then-hope ritual with a sync that proves
convergence.

**Interface.**
```
rig-sync.sh push [PATHS…]     # NAS → rig (default: git diff --name-only HEAD
                              #   plus untracked under hw/ docs/ runbooks/)
rig-sync.sh verify            # hash-compare the tracked set both sides
```

**Behavior.** `push` rsyncs the file list, then auto-runs `verify`: sha1 of
each synced file both sides, print any mismatch, exit 1 on divergence.
`verify` alone compares `git ls-files` (intersection of both trees) — the
campaign's pre-sync spot-check, systematized. Never touches git state on the
rig (twin commits stay a human decision).

**Size.** ~1 hour.

---

## T10. Surface-population inspector  [NEW]

**Purpose.** The aliasing storm was reconstructed *inferentially* from
counters and rate-limited dumps over several hours. A direct dump of the
resident surface list would have named it in minutes.

**Interface.** Sentinel-file trigger (same pattern as T6.1):
`touch /tmp/nv2a-dump-surfaces` → next frame, the pfifo thread writes to
stderr:

```
nv2a-surfaces: 7 resident, 3 invalid, gen=1234
  0: 03264000 1800x1800 p=3648 D16      zeta lin draw=4512 frame=141 feedback rest=GENERAL
  1: 032e4000  128x128  p=512  B8G8R8A8 col  swz draw=4520 frame=141          rest=ATTACH
  …
nv2a-texcache: 214 nodes, 12 borrowed
  top borrows: 03264000(D16 via Y16f) x1 refresh/frame; …
```

One line per resident surface (addr, dims, pitch, host format, color/zeta,
swizzle, draw_time, frame_time, feedback flag, tracked layout) and a
texture-cache summary (node count, borrowed count, top surface-fed bindings
by per-frame refresh). Reuses the existing `QTAILQ_FOREACH(r->surfaces)`
and texture-cache LRU walks — read-only, pfifo-thread, ~50 lines.

**Acceptance.** Triggered during the LoC snapshot scene, the dump must show
the 1800×1800 zeta and its aliased children coexisting with the documented
addresses — the storm's whole cast, in one screenful.

**Size.** 2 hours. Highest insight-per-line of the new tools.

---

## T11. Golden counter baselines for regression gates  [NEW]

**Purpose.** The SB gate today is a human eyeballing "16626/99/99 looks like
last time." Freeze it.

**Interface.**
```
tools/perf/counter-gate.py check BASELINE.tsv RESULT.result
tools/perf/counter-gate.py record BASELINE.tsv RESULT.result
```
`BASELINE.tsv`: `counter <TAB> expected <TAB> tolerance` where tolerance is
`±N`, `±N%`, or `exact`. The SB default-flags baseline starts as:
`SURF_TO_TEX 16626 ±2% / SURF_TO_TEX_FALLBACK 99 ±5 / S2T_MISS_NOSURF 99 ±5 /
mspf_med 33.3 ±0.5` (values from four independent gate runs this campaign —
they were identical to the counter each time, so tight tolerances are
justified).

**Behavior.** `check` parses the T3 `.result` totals/dist blocks, compares,
prints per-row PASS/FAIL, exit 1 on any FAIL. A counter present in the result
but absent from baseline is reported as `NEW` (warning, not failure) — new
counters appear every campaign.

**Consumers.** `sb-regression-check.sh` gains a final `counter-gate.py check`
step; gate runs become pass/fail instead of read-and-nod.

**Size.** 2 hours including recording the SB baselines from the four logs
already on the rig.

---

## T12. Vblank-ladder annotation in the phase line  [NEW — smallest]

**Purpose.** Make the quantization ladder (lessons §1.8) visible at the
moment of measurement instead of requiring arithmetic.

**Behavior.** When `XEMU_PHASE_TIMERS=1`, append to each `phase:` line:
`tick=3/60Hz next=30fps@-16.6ms` — current tick multiple, and the work delta
to the next fps tier (`mspf - (ticks-1)*16.7`). Pure formatting on data the
line already carries; consumers (T3's dist block) can surface "median tier"
and "median distance to next tier" — which is the actual optimization target
number for the 20→30 push.

**Size.** 30 minutes.

---

## Build order (dependency-aware)

| phase | tools | rationale |
|---|---|---|
| 1 | **T1 → T2, T5.1, T7** | correctness floor: working validation, armed-detector rule, strict warnings, safe H-2 — all cheap, everything else reports through them |
| 2 | **T3 (+T12)** | the harness every later tool plugs into |
| 3 | **T10, T11, T4** | insight (surface inspector) + mechanical gates (counters, pixels) |
| 4 | **T6, T5.2, T8, T9** | profiler quality-of-life, pre-commit check, per-title hygiene |

Total estimated effort: ~3–4 focused days for the full set; phase 1 alone is
half a day and removes the two worst classes of self-deception this campaign
actually suffered (unarmed detectors, hidden compiler errors).
