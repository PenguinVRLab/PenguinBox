# Tooling gaps — what the LoC campaign had to work around

Compiled 2026-07-27 at the close of the Line of Contact campaign. Each entry:
what's missing, what it cost us, and a concrete proposed fix sized roughly.
Ordered by how much the gap actually hurt. Companions:
[`loc/02-lessons-learned.md`](loc/02-lessons-learned.md) (the failure catalog
these gaps map onto) and [`tool-specs.md`](tool-specs.md) —
**implementation-ready specs for every fix below plus three new tools**
(T1–T12, with interfaces, acceptance criteria, and a dependency-aware build
order).

---

## G1. Vulkan validation layers do not work — CRITICAL

**Gap:** `display.vulkan.validation_layers = true` in the config, the Khronos
layer installed (`/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json`),
and still **zero validation output** in any run. Never diagnosed.

**What it cost:** An entire campaign of Vulkan surgery — layout models,
borrowed image views, descriptor layout changes, new barrier pairs — shipped
with **no independent correctness checking at all**. Worse, it produced
actively false confidence: every "0 VK validation errors" line in the early
record was a grep for messages that could not exist. One shipped bug (the
shadow double-decode) is exactly the class validation might have flagged
(or at minimum, the missing-transition-pair boot assert would have been a
validation error long before it was an abort).

**Proposed fix (investigate in this order):**
1. Trace `instance.c`: does `enable_validation` actually reach
   `vkCreateInstance`'s `ppEnabledLayerNames`? Is the debug-utils messenger
   created and pointed at stderr? Is there a severity filter eating
   everything?
2. Bypass test: `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
   VK_LOADER_DEBUG=all` in a launcher env — if messages appear, the bug is in
   xemu's plumbing; if not, it's the loader/environment (PRIME/GLVND wrinkles
   on the rig are a suspect).
3. Once working: add a **detector-armed assertion** to every report script —
   refuse to print an error count unless the layer's startup banner is present
   in the log (see G2).

**Size:** hours, not days. Do it before any further renderer work.

## G2. Report scripts can report "0 errors" from an unarmed detector

**Gap:** The bench/report scripts grep logs for `Validation Error|VUID` and
print the count. Zero-because-clean and zero-because-detector-off are
indistinguishable.

**What it cost:** Weeks of record now carry struck-through claims; trust in
every "verified" line had to be re-litigated at handoff.

**Proposed fix:** A shared `report-lib.sh` helper:
`vk_error_count LOG` → prints the count **only if** the validation banner is
found, else prints `DETECTOR-OFF` and exits nonzero. Same principle
generalized: any "no X detected" claim in a script must first check the
detector's arming line. (This is the armed-detectors rule from the SB
campaign's measurement discipline, which we knew and still violated —
automation is the only durable enforcement.)

**Size:** an hour.

## G3. No visual regression harness — content bugs are invisible

**Gap:** We can capture screenshots (`lane4-grab.sh`) but have no golden-frame
compare. Counters measure *work*, never *meaning*: the shadow double-decode
produced numerically perfect frame times and required a human noticing bad
pixels.

**What it cost:** The only real regression of the campaign shipped, and was
caught by luck-plus-owner-diligence rather than process.

**Proposed fix:** `visual-gate.sh <flags…>`: boot → monitor-loadvm the
benchmark snapshot → settle N frames → `lane4-grab` → compare against a
stored golden PNG for that (snapshot, flag-set) pair. Compare with a
perceptual metric (ImageMagick `compare -metric DSSIM` is on tap) and a
generous threshold — the goal is catching *broken shadows*, not dithering
noise. Golden refresh is a deliberate, reviewed act. The deterministic
snapshot (§3.1 of lessons) makes this feasible — same scene every run.
Run it as part of the gate ritual next to the SB regression check.

**Size:** half a day, mostly threshold tuning.

## G4. Guest profiler: rolling ring + no reliable dump points

**Gap:** `xemu-guest-profile` flushes a rolling ring every N flips,
overwriting the dump file; nothing dumps at a chosen moment; `pkill` loses the
tail.

**What it cost:** The first profile "showed" 96% kernel idle — post-scene menu
dilution — inverting the CPU diagnosis until a mid-scene `cp` of the bin
showed 86% *game code*. An hour of wrong theory.

**Proposed fix:** (a) a monitor command (or SIGUSR1 handler) forcing an
immediate flush — "dump now" while the interesting scene runs; (b) segment
dumps named by flip-count window instead of one overwritten file, so
post-hoc analysis picks the window; (c) document loudly in the tool header
that end-of-run bins are diluted. Also fold the PC-histogram one-liner
(region split, top buckets, top pages) into `tools/perf/` as
`guest_profile_histo.py` — it was rewritten inline twice.

**Size:** (a)+(c) an hour; (b) a couple hours.

## G5. No modern-compiler gate before commit

**Gap:** The rig builds with GCC 13 (implicit function declarations = warning);
the win64 cross image has GCC 16 (= error, default since GCC 14). Nothing runs
the strict compiler automatically, and warnings scroll past in a 2000-target
build log.

**What it cost:** A missing prototype merged to `main` and broke the release
build after the merge (found only because we happened to cut a release).

**Proposed fix, cheapest first:**
1. Add `-Werror=implicit-function-declaration` (and
   `implicit-int`) to the rig build's cflags — makes GCC 13 behave like 16 for
   this class at zero infra cost.
2. A `precommit-cross-check.sh`: compile just the touched `.c` files inside
   the `xemu-win64-cross` image (`podman run … x86_64-w64-mingw32.static-gcc
   -fsyntax-only` with the build's include dirs) — seconds per file, no full
   build.
3. Longer term: a CI job on the org repo running both builds per PR (the
   fork deleted its workflows; restore a minimal one).

**Size:** (1) minutes; (2) an hour; (3) separate project.

## G6. `build-release.sh linux` builds an artifact nobody ships

**Gap:** The harness's linux path produces a **flatpak**; the artifact
actually attached to releases is `PenguinBox-x86_64.AppImage`, built outside
the harness by hand. The harness also does nothing to guarantee libusb
(see `loc/HANDOFF-packaging.md` for the full libusb story — `feature:'auto'`
silently drops USB passthrough, the one feature a real Steel Battalion
controller needs).

**What it cost:** The shipped AppImage's provenance and contents aren't
reproducible from the repo; the libusb omission reached a community member
before it reached us.

**Proposed fix:** Replace (or supplement) the flatpak path with upstream's
recipe (`build-linux.yml`): deb → AppDir → `linuxdeploy --output appimage`;
add `--enable-libusb` to `build.sh`; add a post-build assertion that
`squashfs-root/usr/lib` contains `libusb-1.0.so*` and the meson summary line
said `libusb: YES`. **Handed off to the packaging agent** — documented, not
applied.

**Size:** half a day for whoever owns packaging.

## G7. The bench/watch harness is ad-hoc grep scripts

**Gap:** Every measurement round hand-rolled the same shape: launcher variant
(env flags + log name), a watcher (grep for a scene signature, sleep, report
block), an awk counter-summarizer, a per-frame correlation dump. By campaign
end the rig had ~10 near-duplicate scripts (`loc-bench-{off,ext,zc,alias,fb,
cs,gp2}.sh`, watchers, reporters) differing by an env var and a filename.

**What it cost:** Each new lever cost ~15 minutes of script surgery and one
full mistake (a sed multiline edit that had to be redone in python); the
scripts drifted (some kill the session at the end, some don't).

**Proposed fix:** One `perf-bench.sh` on the rig:
`perf-bench.sh --flags "A=1 B=1" --snapshot vm-XXXX --window 90 --out NAME`
→ boots, monitor-loadvms, waits, emits a structured report (counter totals,
per-frame correlation table, mspf distribution, detector-armed VK count) to
`NAME.result` with the `-DONE` sentinel. Launcher variants become arguments,
not files. The report format we converged on (totals + worst-frames table +
distribution) is worth freezing — every verdict this campaign was read from
exactly those three blocks.

**Size:** half a day; retires ~10 scripts.

## G8. H-2 (live-session check) is a convention, not a tool

**Gap:** The check is a hand-typed pgrep triplet with two known traps (the
15-char `comm` limit forcing `-f`; same-compound argv ghost matches — both
documented in the runbook, both still easy to retype wrong).

**What it cost:** One false "live session" scare mid-campaign (ghost match
from a `strings` command), costing a cautious stop-and-verify cycle.

**Proposed fix:** `~/h2-check.sh` on the rig: exactly the three pgreps, alone,
correct flags, exit code 0 = clear / 1 = live (with what's live on stdout).
Callers get one command with no compound to contaminate. Add to the runbook as
*the* H-2 form.

**Size:** 15 minutes.

## G9. Rig-side git is second-class ("twin commits")

**Gap:** The rig checkout is on a different branch with different history;
sync during the campaign was `scp` of changed files + a hand-written "rig twin
of <hash>" commit. Works, but the twin commits have different hashes/content
granularity, and nothing verifies the trees actually match after sync.

**What it cost:** Low this campaign (hash-compared files before the first
sync), but it's a standing drift risk and made every sync a two-step manual
ritual (~9 times).

**Proposed fix:** Either push-over-ssh per the established ISS-B09 flow with a
work branch the rig checks out (making rig commits *the same* commits), or a
`rig-sync.sh` that rsyncs the tracked-file set and prints a
`git diff --stat` both sides to prove convergence.

**Size:** an hour.

## G10. Per-title flag profiles don't exist

**Gap:** The right flag set is per-game knowledge (LoC wants all five; SB
must stay default until eyeballed), carried in docs and launcher scripts
only. The `sb-floor-s1.toml` config is silently doing double duty for LoC —
including an input map bound only for the SB startup ritual — which blocked
reaching interactive gameplay without anyone noticing for a full day. (The
"scancode-18 collision" originally blamed here was a mischaracterization —
see docs/internal/B6-input-stack.md P3: maps are driver-selected, never merged;
the actual dead-end is that main/sub-weapon and lock-on are mouse-hardwired
with no keyboard bindings, plus the silent Duke driver fallback.)

**What it cost:** Benchmarks all ran on attract/cutscene scenes; "PRESS MAIN
WEAPON BUTTON" sat unanswerable on screen through the whole campaign.

**Proposed fix:** Per-title config + env-profile files
(`configs/loc.toml` + `configs/loc.env`) selected by the launcher; a full SBC
input map for LoC (fixing the scancode-18 collision); a comment header in
each stating what's verified on that title.

**Size:** an hour, plus an owner input-mapping session.

---

## Summary table

| # | Gap | Cost realized | Fix size | Priority |
|---|---|---|---|---|
| G1 | Validation layers dead | Zero correctness checking; false claims | hours | **Do first** |
| G2 | Unarmed-detector "0 errors" | Record had to be corrected | 1 h | With G1 |
| G3 | No visual regression gate | Shadow bug shipped | ½ day | High |
| G4 | Profiler ring/dump points | Inverted CPU diagnosis | 1–3 h | Medium |
| G5 | No strict-compiler gate | Broken build merged | min–1 h | High, cheap |
| G6 | Linux artifact mismatch + libusb | Community-visible feature loss | ½ day | Handed off |
| G7 | Ad-hoc bench scripts | Friction each iteration | ½ day | Medium |
| G8 | H-2 by hand | False live-session scare | 15 min | Cheap |
| G9 | Rig git twinning | Drift risk, manual ritual | 1 h | Low |
| G10 | No per-title profiles | Benchmarked attract only; input dead-end | 1 h + owner | Medium |
