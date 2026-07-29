# Spec — Performance lever reference (the env-flag stack)

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| | |
|---|---|
| Tier | A (fork-original; every flag below is ours) |
| Code | gates spread across `vk/*.c`, `pgraph.c`, `shaders.c`, `system/physmem.c`, `xbox-game-hacks.c` — each lever's gate cited below |
| Status | v1 draft |
| Last verified | 2026-07-27 (defaults re-read from code this date) |

## Purpose & scope

Single reference for **every performance lever** in the fork: its env
contract, default state, where it hooks, what observes it, and where it has
been validated. The levers exist because per-title behavior varies too much
for safe global defaults — the design doctrine is:

1. **Env-gated, getenv-once** (process lifetime; three exceptions use
   once-per-frame watch-file toggles for live A/B).
2. **Kill switch (`XEMU_NO_*`) always wins** over the enabler.
3. **Announce on stderr when engaged** — the armed-detector rule: scripts must
   see the banner before trusting flag-dependent results.
4. **OFF path byte-identical** to the code before the lever existed.

⚠ **Not every lever defaults OFF.** Two SB-era features ship default-ON with
kill switches (predictive readback, tex-dirty-clear) and one uses an
inverted-sense toggle file (narrow-fence). The table is authoritative;
"everything defaults off" is a common false memory — it was checked against
code on 2026-07-27.

## The master table

| Lever | Enabler / kill | Default | Gate | One-line contract |
|---|---|---|---|---|
| TLB fastpath | `XEMU_TLB_FASTPATH` / `XEMU_NO_TLB_FASTPATH` | OFF | `system/physmem.c:868` | Fast-path NV2A VRAM access-callback checks in the softmmu hot path |
| Frame overlap (S1) | `XEMU_FRAME_OVERLAP` / `NO_` | OFF | `vk/draw.c` | Overlap-ready frame lifecycle: record next frame while GPU runs previous |
| Async present (S2) | `XEMU_ASYNC_PRESENT` / `NO_` | OFF | `vk/display.c` | Present decoupled from the render thread (removes the flip stall leg). Toggle file: `XEMU_ASYNCPRESENT_TOGGLE_FILE` |
| Async queries (S3) | `XEMU_ASYNC_QUERIES` / `NO_` | OFF | `vk/reports.c` | Stalled occlusion-query resolve made asynchronous |
| Async reports (S4) | `XEMU_ASYNC_REPORTS` / `NO_` | OFF | `vk/reports.c` | Report delivery via queue + 300 µs age-out early submit (RPCS3-style; `renderer.h:295` comment) |
| Batch query reset | `XEMU_BATCH_QRESET` / `NO_` | OFF | `vk/reports.c` | Query-pool resets batched instead of per-query |
| Query ring | `XEMU_QUERY_RING` / `NO_` | OFF | `vk/reports.c` | Ring-buffered query pools (no pool churn) |
| In-batch uploads (S5) | `XEMU_INBATCH_UPLOADS` / `NO_` | OFF | `vk/buffer.c` | Buffer uploads recorded inside the open batch instead of pre-batch fences |
| Pipeline opt | `XEMU_PIPE_OPT` / `NO_` | OFF | `vk/draw.c` | Renderpass/pipeline churn reduction |
| Element batch | `XEMU_ELEMENT_BATCH` / `NO_` | OFF | `pgraph.c:2750` | Batches ARRAY_ELEMENT16/32 method streams into fewer draws |
| Inline batch | `XEMU_INLINE_BATCH` / `NO_` | OFF | `pgraph.c:2941` | Same idea for `NV097_INLINE_ARRAY` streams |
| Const skip | `XEMU_CONST_SKIP` / `NO_` | OFF | `vk/shaders.c` | Skips re-uploading unchanged shader constants |
| Narrow fence | `XEMU_NARROW_FENCE` / `NO_` (env wins) else file `/tmp/narrowfence-on` **present ⇒ ON** | **OFF** (file-armed) | `vk/surface.c:958` | Async early-submit fencing on per-surface `last_write_submit`/`last_use_submit` instead of full-finish waits |
| Predictive readback | kill `XEMU_NO_PREDICTIVE_READBACK`; file `/tmp/predl-off` **present ⇒ OFF** (`XEMU_PREDL_TOGGLE_FILE` overrides path) | **ON** | `vk/surface.c:930` | Unbind-time eager download of CPU-read surfaces so the guest's later read finds fresh RAM (kills the mid-frame read-stall; SB radar) |
| Exact dirty | `XEMU_EXACT_DIRTY` / `NO_` | OFF | `vk/texture.c:437` | Byte-range dirty spans instead of page-bitmap granularity for texture invalidation |
| Tex dirty clear | kill only: `XEMU_NO_TEX_DIRTY_CLEAR` | **ON** | `vk/texture.c:2519` | Clears `possibly_dirty` after a verified cache hit (the hash-leak parity fix; B2/B9) |
| Alias surfaces | `XEMU_ALIAS_SURFACES` / `NO_` | OFF | `vk/surface.c:1110` | Large-parent residency under small overlapping creates (B1/I2) |
| S2T extended | `XEMU_SURF2TEX_EXT` / `NO_` | OFF | `vk/texture.c:1293` | Gather/quadrant/bounce reinterpretation binds (B2/P3.2-4) |
| S2T zero-copy | `XEMU_SURF2TEX_ZEROCOPY` / `NO_` | OFF | `vk/texture.c:1320` | Borrowed-view sampling of exact-match color surfaces (B2/P3.5) |
| S2T feedback | `XEMU_SURF2TEX_FEEDBACK` / `NO_`; **requires ZEROCOPY** (else ignored, with stderr note) | OFF | `vk/texture.c:1344` | Zero-copy sampling of the *bound* target via GENERAL layout (B5) |
| Radar skip | `XEMU_RADAR_SKIP_N` (count), `XEMU_RADAR_SKIP_FILE` (watch, default `/tmp/radar-skip-n`) | OFF | `hw/xbox/xbox-game-hacks.c` | SB radar-readback skip-N experiment lever (A5's framework) |

Instrumentation flags (`XEMU_PHASE_TIMERS`, `XEMU_GUEST_PROFILE*`,
`XEMU_SURF2TEX_DEBUG`, `XEMU_SURF_ACCESS_LOG`, `XEMU_SURF_DUMP`,
`XEMU_SURF_ATTR`, `XEMU_VR_PANEL_DUMP`) are specced in A3, not here.

## Invariants

- **I1 — Kill switch supremacy.** For every paired lever, `XEMU_NO_X` forces
  OFF regardless of `XEMU_X` (several print an explicit override notice, e.g.
  `texture.c:1298-1303`). [VERIFIED — pattern read at every gate cited above]
- **I2 — Arming banner.** Every lever that changes behavior prints an
  identifying stderr line when engaged (and the toggle-file levers print
  every state *change*: `predl:`/`narrowfence:` ON/OFF lines,
  `surface.c:943/978`). A run whose log lacks the banner did not have the
  lever on. [VERIFIED — the LoC/SB bench scripts key on these banners]
- **I3 — OFF-parity.** With a lever forced off, the code path is
  byte-identical to the pre-lever tree. [VERIFIED for the default-OFF set —
  SB gates; for the default-ON pair the *kill switch* restores the historic
  path (predl v1 era / pre-parity-fix hashing) [DERIVED — traced, not gate-run
  with kills engaged]]
- **I4 — Getenv-once.** Env changes after process start have no effect;
  the three watch-file levers re-check **once per frame** from flip
  (`pgraph_vk_refresh_*_toggle`), so hot paths read a cached bool.
  [VERIFIED — code cited]
- **I5 — Dependency.** FEEDBACK ⇒ ZEROCOPY (hard, `texture.c:1352`); ALIAS is
  independent but only pays off alongside EXT/ZC on feedback-pattern titles
  (policy, not code). [VERIFIED / policy noted]

## Per-title verified states (2026-07-27)

| Title | Levers ON (beyond defaults) | Evidence |
|---|---|---|
| Line of Contact | `TLB_FASTPATH SURF2TEX_EXT ALIAS_SURFACES SURF2TEX_ZEROCOPY SURF2TEX_FEEDBACK` | ~1-4 fps → steady ~20 fps; owner-approved visuals (docs/perf/loc/00-baseline.md) |
| Steel Battalion | none — ship defaults (predl ON, tex-dirty-clear ON) | byte-identical gates every campaign round; S2T flags tested "engaged-and-benign" but NOT visually cleared → keep OFF |
| Steel Battalion (candidates) | `ASYNC_QUERIES` works, held OFF pending soak; overlap stack validated stage-by-stage then parked OFF | SB campaign validation matrix |

The launcher scripts are the operative encoding of this table
(`~/coldboot-play-loc.sh`, `~/coldboot-play.sh` on the rig) until per-title
profiles (T8) exist — the config/env split is exactly the gap G10 documents.

## Policies & decision points

- **Why env not config**: levers are experiment-grade; config keys imply
  support. Promotion path: env-flag → per-title profile default (T8) →
  (maybe) config key → (rarely) unconditional. Nothing has passed the final
  step; tex-dirty-clear and predl sit at "unconditional-with-kill" from the
  SB era [DERIVED — their default-ON predates this doctrine being written
  down; treat as grandfathered, revisit if either is ever implicated in a
  bug].
- **Adding a lever** (the checklist the campaign converged on): getenv-once
  gate + NO_ kill + stderr banner + OFF-parity + a reason-counter on every
  rejecting branch + an entry in this table, same PR.
- **Toggle files** exist only where mid-session A/B was operationally
  necessary (present-stall and readback experiments need the same scene
  before/after); their inverted senses differ (predl: file=OFF;
  narrowfence: file=ON) — an acknowledged wart, kept for script
  compatibility. Watch out for stale `/tmp` files arming/disarming levers
  across reboots of unrelated sessions.

## Failure modes

- **Stale toggle file** — `/tmp/narrowfence-on` or `/tmp/predl-off` left
  from an old experiment silently changes the next session's behavior; the
  per-change banner (I2) is the tell. Bench scripts should assert the
  expected state line.
- **Flag typo** ⇒ silently OFF (getenv exact-match). The banner's absence is
  the only signal — another reason scripts must grep banners, not assume.
- **Default drift** — a future edit flipping a default without updating this
  table recreates the "false memory" hazard the warning box addresses.
  Spec-with-change rule applies to the table itself.

## Instrumentation

Each lever family's dedicated counters: S2T/ZC/ALIAS families (B2), overlap
stack counters (B4), `SURF_DOWNLOAD`/predl attribution fields
(`renderer.h:156-170`). The phase-timer `mspf` line is the common
outcome metric; the per-title bench scripts (G7→T3) are the harness.

## Verified-by

- SB campaign: OFF-parity matrix + stage-by-stage ON validation (session
  tasks #1–#7), radar byte-diff gate for readback-correctness levers.
- LoC campaign: A/B ladder per lever (`loc-bench-*.sh`), counters +
  banner-armed logs, final owner sign-off.
- Defaults in this table: re-read from the gates on 2026-07-27 (cited lines).

## Confidence & unknowns

1. [ASSUMED] The default-ON pair (predl, tex-dirty-clear) is safe for ALL
   titles — both shipped after SB-focused validation; no cross-title sweep
   exists. If a new title shows readback anomalies or texture staleness,
   kill these FIRST (`XEMU_NO_PREDICTIVE_READBACK`,
   `XEMU_NO_TEX_DIRTY_CLEAR`).
2. [ASSUMED] Levers compose freely — the LoC five are proven together;
   overlap-stack × S2T-stack composition has never run as one set.
3. [DERIVED] `PIPE_OPT`/`FRAME_OVERLAP` internal contracts are summarized
   from the SB campaign docs (docs/vr/present-wall-plan.md,
   docs/vr/overlap-present-path.md); B4 owns their mechanics — this table
   intentionally stays at the env-contract level.
4. The overlap stack's parked-OFF status on SB conflates "not needed at
   30-lock" with "not proven beneficial elsewhere" — first non-fixed-30
   pfifo-bound title should re-run the ladder (playbook §outcome matrix).
