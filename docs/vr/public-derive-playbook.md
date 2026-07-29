# Standing up a public product repo — the PenguinScreen2 playbook

**What this is:** the *executed* sequence pcsx2-VR used to stand up **PenguinScreen2**
as a public GitHub repository, recorded here as the template for doing the same
with **PenguinBox** (this repo's public product). Not a proposal — this shipped.

**Sources** (sibling private repo `pcsx2-VR`): `docs/vr/release-topology.md`,
`docs/vr/penguinscreen2-derive-plan.md`, `runbooks/derive-penguinscreen2.md`,
and the working tooling in `tools/release/`.

**Repo reality (verified 2026-07-26).** The dev repos — `xemu_vr`, `pcsx2-VR`,
`retro-vr-framework` — are all **PRIVATE**. The public product repos are
**separate and already exist**: `PenguinScreen2`, `PenguinScreen1`,
`PenguinScreen64`, `PenguinScreen64-video-plugin`, `PenguinVR`, and **`PenguinBox`**
(ours). ⚠️ This repo's `CLAUDE.md` still claims *"this repo is PUBLIC"* — that is
**stale** and worth correcting, because it causes agents either to over-restrict
what they write here, or (worse) to reason about the wrong repo entirely.

---

## 1. The one idea everything else follows from

**The public repo is a DERIVED TREE, never a mirror, never a fork.**

You do not make this repo public. You do not scrub it. You *generate* a fresh
tree from an allowlist, rebrand product strings, sanitize the data, and push it as
a **single initial commit with no history** — because the history *is* the secret.

Five requirements govern it:

| | requirement |
|---|---|
| **REQ-1** | Derived tree, never a mirror |
| **REQ-2** | **Allowlist, never blocklist** — a blocklist fails *open*; one forgotten exclusion leaks the moat |
| **REQ-3** | **The profile-authoring / RE tooling NEVER ships.** Not curated, not a subset, not "just the harmless ones." None of it, for any core |
| **REQ-4** | **Idempotent** — re-derive must produce a byte-identical tree |
| **REQ-5** | **Rebrand = product strings ONLY.** Never licenses, never SPDX, never upstream copyright, never the built-on attribution |

REQ-3 is the business case stated as a rule: *"novelty clones in a weekend; the
grind does not."* The public product **consumes** finished profiles; it never
contains the machinery that produces them.

**Permanently dev-only, every core:** `tools/vr/` (the moat), `tools/release/`
itself, `docs/vr/`, `docs/features/`, `AGENTS.md`, `CLAUDE.md`, `RESEARCH.md`,
`KNOWN_ISSUES.md`, `CURRENT_WORKING_LOG.md`, `TEST_QUEUE.md`, `FUTURE_STATE.md`,
`runbooks/`, `.claude/`, `.github/`, every RE artifact (save states, RAM/GS dumps,
ELF symbol tables), and anything Wave-3. **User-facing docs are written FRESH for
the public repo — never copied.**

## 2. What actually happened, with dates

| date | event |
|---|---|
| 2026-07-16 | Topology + derive plan written (`release-topology.md`, `penguinscreen2-derive-plan.md`) |
| 2026-07-16 **same evening** | **Phase 2 BUILT and self-tested** — full dry run derived **3,263 files, tripwire-clean**, and the derived tree **built end-to-end on the rig** |
| 2026-07-17 | Owner decrees: per-file profiles are a **ship blocker**; fact-check gaps closed (flatpak bundling, fresh-history push mechanics) |
| **2026-07-20** | **Public `PenguinScreen2` repo created** — first commit is literally `Initial commit` |
| 2026-07-26 | Still being pushed; tooling actively maintained |

**Total effort estimate that held:** ~3–4 focused days to a private, proven,
flip-ready artifact — deliberately parallelizable with the feature work that
gates the actual public date.

## 3. Phase 1 — prerequisite engineering (do this first, it ships value anyway)

For PCSX2 this was **splitting one monolithic `vr-profiles.yaml` into a folder of
per-serial files** (`SLUS-20919.yaml`). It was chosen first because it is the
**seam the sanitizer needs**, and it's user-visible value on its own: drop one
file in, get one game.

Load-bearing details that generalize:
- **Filename ↔ key mismatch: trust the KEY**, warn on mismatch (filename is a
  deploy convenience).
- **Duplicate key: first wins**, deterministic via sorted filename order.
- **`_`-prefixed files skipped** — `_schema.yaml` holds the header/schema block.
- **Legacy fallback** — if the folder is absent, load the old monolith.
- **User override dir with user-wins precedence** — this became the entire
  delivery mechanism for subscriber profiles *and* the settings-menu browser for
  free.
- **0..N files is a valid state** — an empty folder is an empty catalog and must
  boot clean.

**Lesson for PenguinBox:** identify the equivalent seam *before* building the
derive. Ours is the Title-ID-keyed profile DB (`docs/features/sbc-analog-input.md`
§8) — same shape, same reason.

## 4. Phase 2 — the derive pipeline (`tools/release/`, NOT `tools/vr/`)

The real, shipped tooling — note the deliberate separate home, because
`tools/vr/` is the moat and never crosses:

| file | role |
|---|---|
| `derive.py` | **~50 KB** — the whole pipeline |
| `allowlist.txt` | REQ-2 include-list |
| `rename-map.yaml` | product-string rebrand map |
| `tripwire.txt` | banned-strings audit |
| `public/` | fresh README / build / doc templates |
| `assets/` | brand asset swap (icons) |
| `ship-profiles.txt` / `subscriber-profiles.txt` | which profiles cross |
| `stage-flatpak-builder.sh`, `rig-flatpak-build.sh`, `stage-usb.sh` | packaging + field staging |

### 4a. Allowlist
Crosses: product source, `common/`, `3rdparty/`, `cmake/`, the Qt frontend, build
files, resources **with profiles passing through the sanitizer**, and freshly
written public docs. **Output is built from nothing but the allowlist** — a fresh
export with **no git history**.

### 4b. Rebrand map — strings only
New reverse-DNS app ID; icon set; window title; About dialog **with prominent
built-on attribution** (H-7: rename the product, never the story); setup-wizard
and OSD strings; support URLs → our channels; **its own config dir** (never
read/write upstream's); **auto-updater disabled or repointed** — must never pull
upstream builds; Discord-RPC / achievement identities re-keyed or disabled.

**Must not touch:** `COPYING.*`, every `SPDX-License-Identifier`, upstream
copyright headers, internal namespaces/symbols, data licenses.

**Discipline that caught real bugs:** the map **refuses entries without an audited
`files:` list**, and **zero hits on a settled entry = drift warning, FATAL on
release runs.** Placeholders warn-and-skip until `--require-rebrand-complete`,
which also demands a clean tree.

### 4c. Profile sanitizer
Strips **every comment and every `notes:` field** per file (method narration is
the moat), injects a customize-via-user-folder header, verifies semantically
(output == source minus notes) **per file, every run**, and emits deterministically
so re-derives diff cleanly.

> **It is text-based ON PURPOSE.** A yaml round-trip would re-emit hex as decimal,
> and the catalog's parser reads bare ints as hex — silent, total data corruption.
> *(The "S1 trap.")*

### 4d. Tripwire
A banned-strings scan over the derived tree — tool names, `AGENTS.md`, Wave-3
vocabulary, **rig hostnames/IPs**, hunt vocabulary, `Co-Authored-By`. **Any hit
fails the derive.** It caught **three real issues on its first run**, including a
fork `.gitignore` and tool names leaking via a `LICENSE.note`.

## 5. Phase 3 — stand up the repo

1. **Create the GitHub repo PRIVATE first.** The public flip is *the launch act,
   not the build act.*
2. **Zero GitHub Actions at first push** (a dev-repo lesson). Add exactly one
   fresh minimal build workflow written *for* the public repo.
3. Derive → push → full build on both platforms → run its tests → license
   inventory → manual compliance walkthrough.
4. **Dry-run the RE-derive** — trivial dev change, re-derive, confirm a clean
   idempotent diff. That is REQ-4's only real test.
5. Flatpak validation on **both** target hosts.
6. **Hold.** Flip when the ship gates close.

## 6. The verify gauntlet (every release, no exceptions)

1. Build the derived tree, **both platforms**.
2. **Parse the sanitized catalog with the REAL parser, not pyyaml.** Swap it into
   the dev checkout, run the catalog test, swap back and md5-verify the restore.
   ⚠️ **Dev invariants ≠ public invariants** — the `notes`-provenance assertions
   are *supposed* to fail on a sanitized catalog. Only a parse error or a value
   mismatch is a real defect.
3. License inventory — `COPYING.GPLv3` present, SPDX headers survived, About box
   renders upstream attribution.
4. Flatpak build + validation on both hosts.
5. Populate `STACK.md` for the release — toolchain, pinned deps, validated VR
   runtime versions. Facts sourced from runbooks, **written fresh** (runbooks
   never cross).
6. **Re-derive idempotence** — run twice, tree sha256 must match.

Always start from a **pristine recursive clone at the release tag — never your
working checkout.**

## 7. Package + publish

**Exactly two artifacts per release**, by owner decision:
1. The **flatpak bundle** (tuned for SteamOS, distro-agnostic by construction)
2. The **source archive of that exact derived tree** — which *is* the GPL
   compliance mechanism, satisfied by construction every time

### The field-earned gotchas — this is the valuable part

- **`.DERIVE-INCOMPLETE` marker** — a failed/interrupted derive leaves one. A tree
  carrying it must **never** be packaged; tripwire-failed trees additionally
  *contain* the leak they caught.
- **The flatpak build command produces an ostree repo, NOT a shippable file.** You
  need a second `flatpak build-bundle` step. Pass `--runtime-repo` so the user's
  flatpak can fetch the runtime at install.
- **A bundle carries no remote**, and the **ffmpeg-full add-extension cannot
  auto-install from a bundle** while the build links against it → broken video or
  a silently non-launching app. The end-user docs **must** state the prerequisite
  installs.
- **`git add -A -f` — the `-f` is LOAD-BEARING.** Five crossing nested
  `.gitignore` files silently dropped **7 real files** (including a bundled
  dependency's LICENSE) from a plain `git add -A`.
- **The count gate:** `git ls-files | wc -l` must equal the file count the derive
  printed. That single check proves **repo == archive == manifest**.
- **Record the derive's printed tree sha256** in the release notes, and attach it
  to the GitHub release alongside both artifacts.
- **Fresh history:** `git init` → `git checkout -b main` → one commit. **Both
  identity fields are the owner — never any AI attribution, no trailers.** That
  initial commit is the entire visible history of the product.

## 8. The subscriber channel

Extended profiles ship from a **separate PRIVATE repo** as per-serial sanitized
files (`derive.py --emit-profiles-only`), dropped into the user profiles folder.
Same sanitizer, same verification, same discipline — **a subscriber leak is a
public leak**, and the tripwire applies identically.

## 9. When something must newly cross

Add it to `allowlist.txt` in a **reviewed commit that says why**. Never widen in
the output, never bypass the tripwire, **never publish a tree the script didn't
produce end-to-end.**

---

## 10. What's different for PenguinBox (xemu)

The method ports; these are the deltas to work out:

- **License is different.** xemu/QEMU is **GPLv2**-family, PCSX2 is GPLv3 — the
  compliance text, headers and About attribution differ. Check
  `docs/emulator-core-licensing.md` in the sibling repo, and note that our VR
  code is team-owned and portable while emulator-core diffs are not.
- **Attribution target is xemu/QEMU**, not PCSX2 (H-7 unchanged: rename the
  product, never the story).
- **Our profile seam isn't built yet** — the Title-ID-keyed profile DB is designed
  (`docs/features/sbc-analog-input.md` §8) but unimplemented. **That is Phase 1
  for us**, and per the PCSX2 ordering it comes before any derive work.
- **Firmware posture is sharper.** xemu needs a **BIOS + MCPX dump**; the public
  README must carry the dump-your-own-console line at least as prominently, and
  **ISS-B06** (we currently rely on a *debug* BIOS) is a real onboarding problem
  to resolve before public docs claim a supported path.
- **`tools/vr/` here is already the moat** — `hunt_scan.py` (incl. the new
  `resolve`/`sibling` modes), the dump analysis, the anchor methodology. Same
  REQ-3 treatment: none of it crosses.
- **Steel Battalion adds a new sensitivity**: the SBC device work is partly an
  upstream port (PR #1803). Upstream code crossing back out is fine and expected;
  **our RE and cockpit-design docs are not.**
- **Our tripwire list needs rig facts covered** — the rig address, hostname,
  `RIG_SSH`, `.env.local` contents. PCSX2's list already covers rig hostnames/IPs
  as a category; ours must enumerate *our* values.
