# Steam Deck performance tuning — campaign baseline

Opened 2026-07-28. This is a **new platform axis**, distinct from every prior
campaign: SB and LoC (`docs/perf/loc/`) were both diagnosed and fixed
entirely on **the rig** (NVIDIA RTX 4050 via PRIME offload, X11 desktop). Every
measurement discipline, every trap in `runbooks/rig-perf-measurement.md`, and
every counter baseline to date assumes that hardware. This document exists
because a Steam Deck is a different enough machine — different GPU vendor,
different driver stack, no PRIME split, a compositor in the loop, a hard power
envelope — that rig-derived verdicts **do not automatically transfer**, and
this campaign needs its own baseline before anyone claims a fix "works on
Deck."

Scope note: this is a **platform** campaign, not a **title** campaign — it
sits alongside `docs/perf-per-title-playbook.md` (per-game triage) as a
different axis of variation. A title's playbook entry and its Deck behavior
are two separate things that both need recording.

## TL;DR

One field data point so far: a screenshot showing Line of Contact's
already-diagnosed pre-fix wall (MSPF 750, 751 renderpasses/frame) reproducing
on a Steam Deck. **The "why" for this specific screenshot is now resolved and
turned out not to be Deck-specific at all** — see H1 below: the fix is
present in the current shipped build, but all five levers default OFF with no
UI to enable them, so *any* normal launch on *any* platform reproduces this.
The remaining hypotheses (H2 onward) are still open, genuine Deck-platform
questions for whenever someone actually tests with the levers on.

## Session / provenance

- **Platform**: Steam Deck (Valve). AMD "Van Gogh" APU — Zen 2 CPU cores +
  RDNA2 iGPU (8 CUs), LPDDR5 unified memory, SteamOS (Arch-based Linux). Power
  envelope is a hard ~15 W-class TDP (boostable), handheld thermals — nothing
  like the rig's discrete-GPU headroom.
- **Contrast with every prior baseline**: the rig is NVIDIA (proprietary
  driver) + PRIME offload (iGPU presents, discrete GPU renders) + bare X11,
  no compositor. Steam Deck is AMD Mesa (RADV Vulkan + radeonsi GL) + a single
  GPU (no PRIME dance at all) + normally runs under **gamescope**, SteamOS's
  compositor, in Gaming Mode.
- **First data point**: owner-supplied screenshot (2026-07-28), analyzed and
  logged in `docs/perf/loc/00-baseline.md` § "Field observation (2026-07-28)"
  — that entry has the full counter transcription; this doc does not
  duplicate it, only adds the platform fact and what it implies here.
  Counters, for reference: **MSPF 750, PIPELINE_RENDERPASSES 751,
  FINISH_SURFACE_DOWN 71, FPS 1** — landing almost exactly on the *pre-fix*
  rig baseline (~700 renderpasses, ~750 ms worst frames), not the v9.4
  verdict (20–23 renderpasses, med 50.0 ms).

## Measured (not inferred)

Only the one data point above. Everything below this line is a hypothesis,
tagged as such — this campaign has not yet had a real test session.

## Hypotheses (unverified — flagged as such)

- **H1 — RESOLVED (2026-07-28), and it settles the one data point we have.**
  [VERIFIED] Checked directly rather than assumed: downloaded the
  currently-published `PenguinBox-x86_64.AppImage` (asset updated
  2026-07-28T00:44Z, after the LoC merge) and grepped the extracted binary —
  it contains all five lever env-var strings (`XEMU_SURF2TEX_EXT`,
  `_ZEROCOPY`, `_FEEDBACK`, `XEMU_NO_SURF2TEX_FEEDBACK`,
  `XEMU_ALIAS_SURFACES`). **The fix ships.** But every lever defaults OFF
  (`docs/internal/A2-perf-levers.md`) and nothing in the product — no config
  UI, no per-title profile — turns them on. So the screenshot's numbers are
  fully explained with **no Deck-specific cause required**: any normal launch,
  on any platform, on any build since the merge, reproduces exactly this.
  Full finding, now ranked item 1 in the campaign's open threads:
  `docs/perf/loc/02-lessons-learned.md` Part 4. **This means H2–H8 below are
  still genuinely open** — they were never actually tested by this
  screenshot, since the levers were never on.
- **H2 — the VK↔GL interop path has never run off NVIDIA.** [ASSUMED — real
  risk, not yet tested] The display path exports a Vulkan image via
  `VkExportMemoryAllocateInfo`/opaque-FD and imports it into GL with
  `glImportMemoryFdEXT` (mechanics: `docs/internal/C2-ui-component-cards.md`
  Card 1 — **internal-only, not present in a public export of this repo**).
  `HAVE_EXTERNAL_MEMORY` is hardcoded `1` (`vk/renderer.h:44`). Whether Mesa's
  RADV+radeonsi pair (which, unlike NVIDIA/PRIME, share one driver stack) make
  this interop *more* native or whether it hits an unexercised code path is
  unknown either way. **Verify this actually initializes on Deck before
  trusting any other number from one.**
- **H3 — FIXED 2026-07-28, before this campaign ever ran a real Deck session.**
  A failed Vulkan init used to silently rewrite the renderer to OpenGL and
  *persist* that to the config file — one transient first-launch driver
  hiccup on a fresh SteamOS deployment could have converted the install to
  flat/GL forever with only a fading toast, no loud error (was documented as
  a gap in specs `docs/internal/B9-gl-renderer.md` / `B7-config-system.md`).
  Now: `init_renderer` prints a framed stderr banner, a persistent (not
  fading) red warning shows in the Display settings tab and the Video Debug
  overlay for as long as the fallback is active, and the on-disk config
  keeps the originally-requested renderer so the next launch retries Vulkan
  instead of being stuck on GL. Verified only by a clean win64-cross build —
  **still genuinely useful advice for the first real Deck session**: check
  the renderer the session actually initialized on (now visible in-app, not
  just in a log) before trusting any S2T diagnosis, since this fix has never
  been exercised on real Deck hardware.
- **H4 — hardware-class mismatch means rig verdicts don't transfer.** [ASSUMED]
  Even if every S2T fix engages identically and collapses the renderpass
  count the same way, the RDNA2 iGPU is a materially weaker GPU than the rig's
  discrete RTX 4050 — absolute ms-per-renderpass is hardware-dependent. This
  campaign needs **its own** post-fix verdict; "LoC runs at ~20fps" is a
  rig-specific claim until re-measured here.
- **H5 — gamescope is a new variable no prior campaign has faced.** [ASSUMED]
  Every SB/LoC measurement ran on bare X11 with no compositor. Gamescope (SteamOS's
  compositor, active in Gaming Mode) does its own frame-limiting/scaling and
  sits between our present path and the physical display. Untested interaction.
- **H6 — MangoHud's rig-specific unreliability may not apply here, in either
  direction.** [ASSUMED — check, don't assume] The documented MangoHud
  failure (`runbooks/rig-perf-measurement.md` §instrument) is caused
  specifically by the PRIME split — xemu presents via GL on the iGPU while
  NV2A renders offscreen in Vulkan on the discrete GPU, so MangoHud hooks the
  wrong context. Steam Deck has **one** GPU — no PRIME split — so that
  specific failure mode may simply not exist there, and MangoHud (or Steam's
  own overlay) could be a reliable instrument on this platform. Verify before
  trusting it OR before continuing to distrust it out of habit.
- **H7 — build/launch practicalities, unverified.** Does the shipped AppImage
  run cleanly added as a non-Steam game / in Desktop Mode? Some upstream CI
  work enables `x86-64-v3` build flags (AVX2/BMI2/FMA) — Zen 2 supports the
  ISA, but whether *our* build actually targets something compatible has not
  been checked against a real Deck binary.
- **H8 — Steam Input crosses into the input-stack work.** Steam Deck's
  trackpads/gyro/back-buttons go through Valve's own SDL/Steam Input
  translation layer before reaching us. Cross-reference only for now — not a
  perf question, but relevant once the SBC merge layer (spec
  `docs/internal/B6-input-stack.md`) starts accepting third-party gamepad
  sources.

## Open questions → next steps

1. **Get a real test session with the levers actually on.** H1 is resolved:
   the one data point we have used none of the fix. Nothing below this line
   is testable until a session runs the five LoC levers on Deck — this
   campaign otherwise has no Deck-specific measurement discipline yet (the
   rig's traps in `runbooks/rig-perf-measurement.md` should be re-validated,
   not assumed to carry over — H6 is a concrete example of one that might
   not).
2. **Confirm which renderer actually initialized** before trusting any S2T
   diagnosis on this platform (H3) — now visible in-app (Display tab / Video
   Debug overlay) as well as the startup log, since the 2026-07-28 fix, but
   that fix itself has never run on real Deck hardware. Still the single
   highest-priority check for any future Deck session.
3. **Re-run the exact "replay" scene** with the five LoC levers explicitly set
   and `XEMU_SURF2TEX_DEBUG=1`, capturing the **log file** (not just the
   overlay) so the S2T/ALIAS/ZC counter family is visible — this is now the
   *first* real test of the fix on this platform, not a re-test.
4. Once a real session exists: establish this campaign's own counter
   baseline and verdict numbers (H4) — do not inherit the rig's "~20fps"
   claim for LoC as a Deck claim.
5. **Separately, a product decision, not a Deck one:** ranked item 1 in
   `docs/perf/loc/02-lessons-learned.md` — should the LoC levers default ON
   (at least for LoC specifically, title-ID-gated), since as shipped none of
   the five turn on for any normal launch on any platform. Correction (same
   day, owner-reported): this is narrower than "the campaign has no benefit" —
   a real user is getting real improvement from *something* in this body of
   work already; these five flags specifically just aren't it yet.
