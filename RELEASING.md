# RELEASING.md — how PenguinBox ships to the public GitHub

**What this is:** the exact, field-tested procedure by which a core in this
family goes from private dev repo to public repo + release — written from the
run that actually shipped (**PenguinScreen2**, the PS2 core, tag `v1.0-rc`),
including every rule and every mistake that procedure now guards against. The
family-canonical treatment lives in the shared framework repo (docs 14 and 15);
**this file is the PenguinBox-operational copy** so an agent working only in
this repo has the full picture.

Read [AGENTS.md](AGENTS.md) first — its hard rules apply to every step here,
especially H-1 (commit identity) and H-8 (this repo is public).

---

## 0. The model in one sentence

> The public repo is **derived, never mirrored**: a script builds it from
> nothing but an allowlist, sanitizes what ships, stamps the version, and
> **fails hard** if a single banned token appears anywhere in the output —
> cleanliness is the output of a program, not of someone remembering to tidy.

There is no "clean up before pushing." If the output is wrong, you fix the
pipeline and re-derive. The derived tree is disposable and regenerated for
every release.

## 1. The GitHub topology — exactly how PS2 did it

Two accounts are involved, and the distinction is load-bearing:

- **`PenguinVRLab`** — the public face. It is a **separate user account, not
  an organization**: dev-side tokens have read-only access. Repos there are
  created *by the owner from that account* — no automation can do it.
- **`patrickfcarey`** — the working account. Public **staging/fork** repos
  live here; branches are pushed here; PRs go from here to the PenguinVRLab
  twin.

The PS2 sequence, as performed:

1. `PenguinVRLab/PenguinScreen2` created (owner action) with a bare initial
   commit. A personal fork was created from it.
2. The derive produced the public tree from the private dev repo; it was
   committed onto a release branch (`penguinscreen2-1.0-rc`) **on the fork**
   and pushed.
3. **PR #1** opened: fork branch → PenguinVRLab `main` ("initial public
   source drop"). Follow-up fixes (field-test findings, a strict-review pass,
   a sandbox fix) landed as further commits **on the same branch** — the PR
   updates in place.
4. The release (`v1.0-rc`, marked **pre-release**) was cut with the built
   bundle attached as the release asset.
5. Later kit revisions were shipped by **re-deriving and full-content-syncing
   the same branch** (see §4 for what that implies).

**Two hard lessons from how PS2's run deviated — do not repeat:**

- **Merge the PR before any announcement.** PS2's initial-drop PR sat open
  for days with the org repo still empty and the release parked on the fork.
  A fork-hosted release is a bootstrap artifact, not the pattern. For
  PenguinBox: source drop → PR → **merged** → release cut on the
  PenguinVRLab repo.
- PenguinBox's staging repo (`patrickfcarey/PenguinBox`) already exists,
  empty, with an honest "experimental, nothing shipped yet" description. The
  PenguinVRLab twin is an owner action; until it exists, everything lands on
  staging and transfers later.

## 2. The derive — what the pipeline must do (every stage earned)

The reference implementation is the PS2 core's `tools/release/derive.py`
(~1000 lines); the N64 core carries a lean port that is the better starting
point for this repo. Stages, in order, each mapped to the failure it prevents:

| Stage | Rule | The failure it prevents |
|---|---|---|
| Preflight | source must be a clean git checkout; release mode refuses dirty | shipping uncommitted local state |
| Collect | **allowlist-only** (exact paths + `dir/` prefixes); never copy-then-delete | deletion-based cleaning misses one file once, and that once is forever |
| Sanitize shipped data | explicit ship-list; strip research narration — **including trailing/inline comments**, not just comment lines | PS2/N64 catalogs carried measurement dates, capture ids, and struct annotations in *trailing* comments that a line-level strip missed |
| Comment strip (our scope only) | our VR source loses narration comments; SPDX/Copyright always kept; **upstream files ship comments intact** | our comments are RE lab notes; upstream diffs must stay honest |
| Version stamp | write a stamp file and make the build read it | the derived tree has no `.git`; PS2's first field install reported version "Unknown" |
| Fresh public docs | authored-for-public README/QUICKSTART/KNOWN-ISSUES laid in; collision with a shipped file is FATAL; `TODO()` in release mode is FATAL | internal docs must never be the source of public docs; placeholder docs shipped to a tester once |
| **LICENSE is pipeline-owned** | the derive lays `LICENSE` in at the root | PS2's LICENSE existed only branch-side, so a re-derive **silently deleted it** — anything the pipeline doesn't produce will vanish on the next refresh |
| Tripwire | banned tokens checked against every output file's **name and bytes** (ASCII + UTF-16); any hit fails the run; no exception list | it catches humans: it has caught an internal codename in a fresh script comment, internal tool names inside `.gitignore`, and — on the N64 run — tool names passed as **functional strings** (OpenXR app names) |
| Manifest | per-file SHA-256 + one content hash; **derive twice, hashes must match** | "what we reviewed" and "what we shipped" must be provably the same |

**PenguinBox-specific derive additions (non-negotiable, from the licensing
audit):**

- **The GPLv2 wall.** This fork's aggregate license is **GPLv2** (QEMU
  inheritance; v2-only islands). **No GPLv3 code may ever flow in** — not
  from the PS2 core, not from anywhere. Methods and documents are fine;
  code is not. Share direction is one-way: MIT code written here may travel
  out; GPLv3 code from siblings may never travel in.
- **SPDX audit before the first derive:** every VR file *we* authored here
  gets an explicit **MIT** header (MIT flows cleanly into a GPLv2 aggregate
  and stays reusable family-wide). The N64 run found our files had **no
  headers at all** — assume the same here until proven otherwise.
- **License gate in the derive:** fail the run if any file under our VR
  paths carries a `GPL-3.0` SPDX tag. The tripwire scans for secrets, not
  licenses; this gate turns a legal review into a machine check.

## 3. The rules that apply to every public step

1. **Commit identity (H-1)** — on the derived-tree commits too:
   `GIT_COMMITTER_NAME="Patrick Carey" GIT_COMMITTER_EMAIL="patrickfcarey@gmail.com"
   git commit --author="Patrick Carey <patrickfcarey@gmail.com>" …`
   Both fields, no trailers, verified with `git log --format='%h %an / %cn'`.
2. **This repo's H-8, applied to the output:** the public tree must never
   name, reference, or hint at unannounced family/framework work. That is
   what the tripwire enforces mechanically — maintain its token list
   dev-side, and treat a hit as a stop-everything event, not an annoyance.
3. **Honest maturity labels.** PenguinBox is **experimental** and says so in
   the repo description; its README must say **"EXPERIMENTAL — not yet
   field-validated"** in those words and point at the field-validated
   flagship (PenguinScreen2). Claim exactly what's earned — this is H-7
   applied to maturity.
4. **No binaries until field validation.** Source drops are cheap;a binary
   release implies the kit + verification pass behind it. PS2 earned its
   binary through an in-headset validation run and an offline gate suite;
   PenguinBox ships source-only until it has the equivalent.
5. **No secrets in ANY file that crosses** — including `.env.local` values
   (rig addresses live there and in the private lane only; the committed
   `.env.local.example` pattern is the rule here already).

## 4. Refreshing an already-open source drop (how the v3 kit shipped)

When dev-side fixes must reach the public repo, do **not** hand-edit the
public branch. The procedure, exactly as run for PS2:

1. Re-derive from the clean dev tree in release mode. Verify tripwire clean
   and the content hash.
2. Clone the fork's release branch **shallow** into scratch space.
3. **Guarded** full-content sync: verify `.git` exists and the branch is the
   expected one **before** any destructive step, then
   `find . -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +` and copy
   the derived tree in.
4. Commit (H-1 identity), push — the PR updates in place.

**H-10 applies here: do NOT land the refresh as one "sync" commit.** A single
blob per refresh makes the public history useless for the one job it has —
letting a user bisect a regression they are reporting. Stage the derived delta
into logical commits instead (`git add -p` the sanitized tree), each with a
real subject and body, mirroring the public-visible changes rather than the
private commit list. A 1:1 replay is impossible by construction: the derive
strips comments and excludes the private lane, so content differs and many
private commits touch nothing public. Deliberate units, authored for a public
reader, are the requirement — not a mirror.

**Why the guards are written in bold:** during the PS2 v3 refresh, a clone
timed out, the `cd` into the missing directory failed, and the sync commands
ran **in the dev repo** — deleting its top level (recovered via git, but the
untracked build tree was lost). Every destructive shell step is preceded by an
existence check, or it does not run. Related: full-content sync means the
branch becomes *exactly* the derived tree — anything that existed only
branch-side is deleted (that is how PS2's LICENSE vanished; see §2).

## 7. LAUNCH WINDOW — the early-RC mode (added 2026-07-27, from the first drop)

§1–§4 describe **steady state**. They are wrong for a first public drop, and
following them there costs more than it protects. This section is the mode that
applies instead, and H-11 defers to it.

### Why the steady-state flow fails at launch

**Coverage you cannot manufacture.** Some things are untestable in-house *by
construction*: USB passthrough needs a real Steel Battalion controller we do not
own; the OHCI per-endpoint fix only exercises with real hardware and multiple
endpoints; a Windows cross-compile had never been run on Windows by anyone. No
amount of internal review reaches them. **Shipping is the test instrument.**

The first drop produced **four real defects in a day** — missing libusb, the
inert overlay header, the shutdown segfault, and the settings never persisting —
every one from a user doing something we structurally cannot do here. A
derive→PR→merge cycle per fix would have found none of them faster.

**Momentum has a decay rate.** Anticipation around a launch is a real asset and
it is perishable. The steady-state flow trades it for review latency — correct
when there is an installed base to protect, wrong when there is an audience
waiting and nothing yet to regress. *This is the factor §1–§4 do not model.*

### What the window permits

- Direct `gh release upload --clobber` to the release.
- Rebuilding and republishing the same tag repeatedly, same day.
- Editing published notes in place as facts change.
- Shipping a fix before it has been reproduced in-house, **when it is
  independently correct** — the libusb and overlay fixes were both certain on
  inspection and could not wait on hardware we lack.

### What it still requires — non-negotiable, these are what made it work

1. **Verify the artifact before every publish.** Checksum against the build
   machine, then prove the feature is *in the binary* — `ldd`/`nm`/`strings`,
   not "it compiled". Passthrough went missing twice by two different mechanisms
   and only artifact checks caught the second.
2. **Freeze the source tree across a multi-target build.** Both binaries must
   carry the same commit stamp and it must be *checked*, not assumed. That is
   what let a stripped `.exe` prove its own provenance.
3. **Notes track the artifact in the same action.** A stale SHA reads as a
   corrupt or tampered download. This slipped twice on day one.
4. **Honest caveats stay in.** Untested-in-play, glibc floor, not-validation-
   checked. The window buys speed, never overclaiming.
5. **Record the download count before clobbering.** Replacing an asset resets
   its counter, and that was the only usage signal we had — "1 AppImage user"
   became unknowable.
6. **H-1, H-10 still apply.** Fast publishing is not fast committing.

### The window MUST have a stated end

Without an exit criterion "early RC" becomes permanent and H-11 means nothing.

**The window closes when the launch is PROVEN, not when the bug list is empty.**
Its purpose is to reach platforms and hardware we cannot test in-house. Once
users are playing, that purpose is served and the justification expires — every
later fix has an installed base to protect, which is exactly the condition
§1–§4 were written for.

Close when **both** hold:

- [x] Each shipped target has **run for a real user**, not merely built.
- [x] Users can **play**, and the feedback loop is producing reports.

**Explicitly NOT exit criteria** — these would keep the window open forever and
invert its meaning:

- ~~No open defect that only reproduces off-site.~~ ISS-B17 may never reproduce
  here; we own no Steel Battalion controller. A defect that needs *someone
  else's* hardware is a permanent condition, not a launch condition.
- ~~Every untestable feature confirmed.~~ Same trap. Passthrough confirmation
  depends on a user choosing to try it, on their schedule.

Fixes for those still ship — through §1–§4, like any other fix, to users who
now have something worth not breaking.

**On close:** announce it, and revert to §1–§4.

### Status

**CLOSED 2026-07-28.** Opened with the first binary drop on 2026-07-27; closed
once users were playing on both targets.

Its stated purpose was met and then some: four defects surfaced in a day — the
missing libusb, the inert overlay header, the shutdown segfault, and settings
never persisting — none of which internal review could have reached.

Closed on the owner's call, which also corrected the criteria above. The
original set conflated *launch proven* with *all bugs resolved*, and would have
held the window open indefinitely on a defect requiring hardware we do not own.

---

## 5. Release cut (when PenguinBox gets there)

1. PR merged on the PenguinVRLab repo; `main` carries the source.
2. Tag + release **on the PenguinVRLab repo** (pre-release until field
   validation says otherwise), bundle attached, SHA-256 recorded.
3. The install kit is staged by **code, never hands** (the PS2 kit's staging
   script verifies every input, stages atomically, generates the install doc
   to match what shipped, and writes SHA256SUMS) — port it when the first
   binary release is scoped, not before.
4. Verification gates before release #2: the PS2 e2e suite pattern (artifact
   integrity, kit contract, docs-vs-code contract, fork-integrity for carried
   patches). Two doctrines carry over verbatim: **a skip is not a pass**, and
   **a test that cannot fail is not a test** (prove the instrument loaded
   before trusting its verdict).

## 6. PenguinBox pre-flight checklist (state as of 2026-07-26)

- [ ] **SPDX/MIT audit of every VR file we authored here** (§2) — blocks
      everything else.
- [ ] Port the lean derive (N64's `tools/release/` is the template): author
      `allowlist.txt` from `git ls-files` starting from *nothing crosses*;
      author this repo's tripwire token list; add the GPL-3.0 license gate.
- [ ] Public docs shell (README with the experimental label, QUICKSTART,
      KNOWN-ISSUES) — authored fresh, never edited-down internal docs.
- [ ] Derive twice → tripwire clean + identical content hash.
- [ ] Push branch → staging fork → PR → **merge**.
- [ ] Owner action: PenguinVRLab twin repo (transfer or fork-and-PR).
- [ ] No release until the Tier-0 blocker is resolved and a field validation
      pass exists — experimental means experimental.
