# RELEASE-READINESS.md — audit against RELEASING.md

**Audited 2026-07-26 against `origin/docs/releasing-playbook` @ `88228067db`.**
This is the evidence-backed state of [RELEASING.md](RELEASING.md) §6. Nothing
here has been fixed — this is the checklist and the requirements, not the work.

> ## Verdict
>
> **The release cannot be cut, and the gap is bigger than §6 implies.** §6 lists
> 7 items; the audit found **9 blockers**, three of which §6 does not mention at
> all (a committed secret, upstream's donation config, and no defined release
> content) — **plus 3 more in the publication path itself (§P), one of which only
> the owner can clear.** The single largest item is that **the derive pipeline does not
> exist** — §2 describes it in detail and `tools/release/` is empty.
>
> **Good news:** the item §6 calls *"blocks everything else"* is **mostly already
> done**, and the GPLv2 wall is **intact**. See CLEARED.

---

## CLEARED — checked, no action needed

| Claim | Finding |
|---|---|
| *"assume our files have no SPDX headers until proven otherwise"* (§2) | **Wrong for this repo.** All 10 `pgraph/vk/vr_*.{c,h}`, both `sbc-overlay.{cc,hh}`, and both `tools/vr/*hunt_scan.py` carry SPDX **and** Copyright headers. **The N64 finding did not repeat here.** §6's "blocks everything else" item is ~80% complete. |
| The GPLv2 wall | **Intact.** Only two tracked files mention GPLv3 — `include/qemu/host-utils.h` and `util/host-utils.c`, both **upstream QEMU**, both dual-licensed *"GPLv2/LGPLv3"*, so the GPLv2 option is available. **No GPLv3-only code anywhere. Not a violation.** |
| Secrets via `.env.local` | Correctly handled: `/.env.local` is gitignored (`.gitignore:32`), only `.env.local.example` is tracked. **The pattern works — one script bypasses it (B2).** |

## BLOCKERS

### B1 — The derive pipeline does not exist
`tools/release/` is absent. The only artifact is `docs/vr/public-derive-playbook.md`
(a description). **Everything in RELEASING.md §2 is unbuilt**: allowlist, comment
strip, version stamp, tripwire, manifest, twice-derive hash check, GPL-3.0 gate.

**This is the critical path.** Port the N64 lean derive as §2 says.

### B2 — A committed secret: the rig address is hardcoded
```
tools/vr/deploy-to-rig.sh:17:  RIG="${RIG_SSH:-<user>@<rig-ip>}"
```
The `.env.local` mechanism exists precisely to prevent this, and **the fallback
default defeats it**. Both the rig user and IP ship in a tracked file today.

Fix is trivial (fail if `RIG_SSH` unset). **The real requirement: this token must
be in the tripwire list**, so it can never recur. `docs/vr/public-derive-playbook.md:240`
already flags this and was never actioned.

### B3 — No defined release content
**`origin/master` contains zero VR/SBC code** (verified by exact path). The work
is spread across **six divergent branches**, none merged:

| branch | commits ahead of master |
|---|---|
| `sb-graphics-research` | 249 |
| `docs/releasing-playbook` | 110 |
| `kvm-feasibility` | 109 |
| `penguinbox-public-derive` | 105 |
| `steel-battalion` | 104 |
| `graphical-dumps` | 76 |

**There is no branch that represents "what we would ship."** The derive needs a
clean source tree (§2 preflight refuses dirty). **Decide and consolidate first.**

### B4 — License inconsistency across our own files · **provenance RESOLVED 2026-07-26**

> **Provenance cleared — see [LICENSING.md](LICENSING.md).** The sibling PS2 VR
> layer this code descends from is tagged `GPL-3.0`, which read cold looks like a
> wall violation. It is not: that directory is 47 commits by a single author who
> is also this project's author, carries no other copyright line, and no PCSX2
> source was copied into it. A sole copyright holder may license their own work
> differently elsewhere. **Note the derive's SPDX gate could not have caught this
> either way** — our files are tagged `LGPL-2.0-or-later`, so the gate reads
> clean regardless. Only the written record checks provenance.
>
> **What remains is a preference, not a legal question:** which licence for the
> files that are wholly ours.
| files | SPDX |
|---|---|
| `pgraph/vk/vr_*.{c,h}` (10), `tools/vr/*hunt_scan.py` | **LGPL-2.0-or-later** |
| `ui/xui/sbc-overlay.{cc,hh}` | **MIT** |
| RELEASING.md §2 mandates | **MIT** |

**Two different licenses on code we authored, and neither set matches the
playbook.** We hold the copyright, so this is a free decision — but it must be
made deliberately and applied consistently **before** the SPDX gate is written,
or the gate enforces the wrong thing.

### B5 — SPDX gaps on the remaining files
| file | state |
|---|---|
| `hw/xbox/xid-steel-battalion.c` | Copyright ×3, **no SPDX** |
| `tools/vr/apply-ofp-headlook.py` | **neither** |
| `tools/vr/{demo-pbox,deploy-to-rig,flat-launch,rig-boot-test,rig-sync-check,sb-launch}.sh` | **neither** (6 files) |

`xid-steel-battalion.c` is the most important file in the SBC work and is the one
missing its identifier.

### B6 — README is upstream xemu's
`README.md` opens *"Please visit https://xemu.app for more information."*
§2 requires a **freshly authored** public README, and §3.3 requires it to say
**"EXPERIMENTAL — not yet field-validated"** in those words. `QUICKSTART.md` and
`KNOWN-ISSUES.md` are **absent**.

### B7 — `.github/FUNDING.yml` routes donations to upstream
```
patreon: mborgerson
```
Shipping this means **our public repo's Sponsor button funds upstream xemu's
author.** Not malicious, but it must never ship. Likewise
`.github/ISSUE_TEMPLATE/config.yml` points bug reports at **xemu's Discord**
(`discord.gg/ayyjsuM`), and `.github/copilot-instructions.md` is upstream's.

**Requirement: `.github/` is allowlist-excluded by default**, then re-added
deliberately, file by file.

### B8 — LICENSE is not pipeline-owned
`LICENSE` and `COPYING` are tracked today. §2's PS2 lesson is that a LICENSE
which exists only branch-side is **silently deleted by the next re-derive**.
Being tracked in the dev repo is not the same as being produced by the derive.

### B9 — Tier-0 blocker unresolved
§6's own last line gates release on it. It is referenced in
`docs/features/identical-device-identity.md:141` (*"One Tier-0 fix serves the twin
sticks, the wheel and the pedals"*) and **is not in `KNOWN_ISSUES.md`** — so the
gating blocker isn't tracked where blockers are tracked.

---

## P — THE PUBLICATION PATH IS BLOCKED (audited 2026-07-26)

The release must go **fork → PR → merge → release on the PenguinVRLab twin**
(RELEASING.md §1). **It cannot, today.** Two blockers, and the first is an owner
action no automation can perform.

### P1 — `PenguinVRLab/PenguinBox` is completely empty
`GET /repos/PenguinVRLab/PenguinBox/commits` → **409 Git Repository is empty**;
`size: 0`; no default branch; `forks: 0`.

- **You cannot open a PR into a repo with no commits** — there is no base branch
  to target.
- **You cannot fork an empty repo either**, so P2 is gated on this too.
- Dev-side access is `pull: true, **push: false**, admin: false` — exactly the
  read-only posture §1 describes. **Only the owner, from the PenguinVRLab
  account, can create the initial commit.**

### P2 — the staging repo is NOT a fork of the twin
`patrickfcarey/PenguinBox`: `fork: false`, `parent: null`, one commit
(*"Initial commit"*, 2026-07-25, README.md only). Dev-side access is admin.

**GitHub cross-repo PRs require both repos to be in the same fork network.**
Shared history is not sufficient — network membership comes from the fork
operation itself. Confirmed empirically:
`GET /repos/PenguinVRLab/PenguinBox/compare/main...patrickfcarey:main` → **404**.

So even after P1 is fixed, **a PR from the staging repo as it exists today would
be rejected** ("These repositories are entirely unrelated").

**This is where our run has already deviated from PS2.** §1 records that the PS2
twin was created *"with a bare initial commit"* and then *"a personal fork was
created from it."* Here the staging repo was created **independently** instead of
forked — so the documented PR path was never actually available.

### The unblock sequence

1. **OWNER, on the PenguinVRLab account** — give the twin a bare initial commit.
   Web UI is fine (Add a README). *This is the only step requiring you.*
2. Rename `patrickfcarey/PenguinBox` out of the way (dev-side, admin — note the
   token lacks `delete_repo`, so rename rather than delete).
3. `gh repo fork PenguinVRLab/PenguinBox --clone=false` → recreates
   `patrickfcarey/PenguinBox`, this time **inside the fork network**.
4. Derive → commit the derived tree to a release branch **on the fork** → push.
5. PR fork branch → `PenguinVRLab/PenguinBox` main → **merge before announcing**
   (§1's first hard lesson: PS2's PR sat open for days with the twin empty).
6. Cut the release **on the twin**, pre-release, source-only (§3.4).

**Steps 2–6 are mechanical and dev-side. Step 1 is not, and nothing proceeds
without it.**

### P3 — and the derive still cannot run
Independent of topology: `--release` fails at `LICENSE.public` and
`public-docs/`, and the allowlist is unsigned. See B1/B6 and the decisions below.

---

## DECISIONS NEEDED — yours, not mine

1. **License for our files: MIT or LGPL-2.0-or-later?** (B4) The playbook says
   MIT for family-wide reuse; the code says LGPL-2.0-or-later. Both are
   GPLv2-aggregate-compatible, so this is about downstream reuse, not legality.
2. **What is v1's scope?** Tier-1 world-locked VR alone is demonstrable today.
   The SBC work is *not* input-complete — `sbc-overlay.cc` is a **read-only
   monitor** (lamps/meters/gear glyph; no injection), and the real-stick path is
   designed but unwritten. **Shipping "Steel Battalion support" would overclaim
   under §3.3 / H-7.**
3. **Release content branch** (B3) — consolidate to `master`, or cut a dedicated
   release branch?
4. **CI:** workflows were deleted at the merge-base commit (`8469a50a7d`). A
   public repo with no build verification is a choice — make it deliberately.
5. **Source-only confirmed?** §3.4 forbids binaries pre-field-validation. Assume
   source-only for v1 unless you say otherwise.

## BUILD LIST — what the derive needs (B1 expanded)

- [ ] `allowlist.txt` authored from **nothing crosses**, not from `git ls-files`
      minus exclusions (§2: never copy-then-delete)
- [ ] Tripwire token list. **Literals (rig user/address/hostname, NAS name,
      private framework repo name, unannounced roadmap terms) belong in the
      git-ignored `tripwire.local.txt` — never in a tracked file**; tracked
      patterns cover them generically. Names **and** bytes, ASCII + UTF-16.
- [ ] GPL-3.0 SPDX fail gate scoped to our VR paths
- [ ] Comment strip — **including trailing/inline comments** (the PS2/N64 miss);
      our VR sources only, upstream files ship comments intact
- [ ] Version stamp file + build reads it (no `.git` in the derived tree)
- [ ] LICENSE laid in **by the pipeline** (B8)
- [ ] Manifest: per-file SHA-256 + content hash; **derive twice, hashes match**

## What I did NOT check

- **Whether the VR build actually compiles** on any of the six branches.
- **Research narration in shipped code comments** — the volume of strip work is
  unmeasured; §2 says this is where PS2 and N64 both got caught.
- **`scripts/gen-license.py`** is modified (+50/−? vs master) — it may already do
  part of the license work; unread.
- Whether `patrickfcarey/PenguinBox` staging repo state matches §1's description.
- Anything on the rig. **M-7 — the perf-campaign agent still owns it.**
