# The specification plan — documenting what we now own

> **PRIVATE — ours only (owner rule, 2026-07-27).** Everything under
> `docs/internal/` is internal documentation for the private working repo
> (`patrickfcarey/xemu_vr`). It is **never added to the allowlist** that
> merges content over to the public repo (`penguinvrlab/penguinbox`) — no
> exceptions, no partial files. The specs are the ownership advantage; they
> do not ship.

**Premise.** This fork is ours to maintain. Upstream (QEMU → xemu → us) ships
code, not behavioral specifications — every campaign so far has paid a tax
reverse-engineering invariants from source *during* firefights: the surface
cache's ownership model, the texture cache's key semantics, the
surfaces-rest-in-ATTACHMENT layout invariant, the depth-format decode chains,
the SBC input map. Each was learned the expensive way, some twice. This plan
converts that recurring tax into a one-time, compounding asset: **a
specification for every feature we own or touch.**

Written 2026-07-27, immediately after the Line of Contact campaign — the
freshest possible evidence of which missing specs hurt and how.

---

## 1. Scope philosophy: spec by ownership tier

We do **not** spec QEMU. We spec the delta we own and the inherited machinery
we operate, at depth proportional to how much we touch it.

| Tier | What | Depth | Why |
|---|---|---|---|
| **A** | Fork-original code (VR, perf levers, SBC overlay, game hacks, build harness, instrumentation) | **Full behavioral spec** | We wrote it; if we don't spec it, nobody ever will |
| **B** | Inherited xemu subsystems **we modify** (nv2a pgraph/VK internals, input/xid, config, snapshots) | **As-built spec**, derived from code + verified by experiment | These are where our bugs happen — every campaign fight was here |
| **C** | Inherited xemu subsystems we operate but rarely touch (APU/DSP, network, SMBus family, UI scenes, Chihiro) | **One-page component card** | Enough to know what it is, its interfaces, and where the danger is |
| **D** | Generic QEMU (TCG, block, migration core, …) | **Out of scope** — reference upstream docs | Not our delta; upstream documents it |

One escalation rule: **the moment a Tier-C component gets modified, its card
must be upgraded to a Tier-B spec as part of that same change.**

---

## 2. The inventory (surveyed 2026-07-27)

### Tier A — fork-original (full specs)

| # | Feature | Code | Existing doc seeds |
|---|---|---|---|
| A1 | VR subsystem (OpenXR loader/session/compositor, manager, camera, SBC world panel) | `vk/vr_*.{c,h}` (~10 files) | `docs/vr/00-mvp-plan.md`, `01-port-map.md`, field guide |
| A2 | Perf lever stack — S2T family (`EXT`/`ALIAS`/`ZEROCOPY`/`FEEDBACK`), overlap S1–S5, TLB fastpath, radar-skip, const-skip, element-batch | spread through `vk/`, `pgraph.c`, `cputlb.c`, `physmem.c` | `docs/perf/loc/*`, `sb-graphics-findings.md`, overlap docs |
| A3 | Instrumentation — phase timers, nv2a-prof counters, guest profiler, S2T debug dumps | `phase_timers.*`, `debug.h`, `xemu-guest-profile.c` | scattered in perf docs |
| A4 | SBC cockpit overlay (F3) + SBC input mapping | `ui/xui/sbc-overlay.cc`, `ui/xemu-input.c` delta | `docs/features/sbc-*.md` |
| A5 | Game hacks framework | `hw/xbox/xbox-game-hacks.{c,h}` | none |
| A6 | Build/release harness + cross toolchain fixes | `build-release.sh`, `ubuntu-win64-cross/`, `flatpak/` | `HANDOFF-packaging.md` |
| A7 | Perf tooling (T1–T12 as they land) | `tools/perf/`, rig scripts | `tool-specs.md` (already spec-form) |

### Tier B — inherited, modified by us (as-built specs)

| # | Subsystem | Code (approx size) | Campaign scars justifying the spec |
|---|---|---|---|
| B1 | **VK surface cache** — SurfaceBinding lifecycle: create/target-switch/evict/invalidate/migrate, download/upload, dirty tracking, memcb registration, scaling, **now + aliasing & layout tracking** | `vk/surface.c` (~2300 lines) | The entire LoC round 2; the "incompatible-evict bypasses the alias keep" surprise; `size = rows×pitch`; frame_time vs draw_time semantics |
| B2 | **VK texture cache** — TextureKey/TextureBinding, lru + hashing, possibly_dirty, surface-as-texture compat + all five S2T paths, **borrowed nodes** | `vk/texture.c` (~3000 lines) | The entire LoC rounds 1+3; binding_found semantics; dedup keys; descriptor layouts |
| B3 | **Format model** — kelvin↔VkFormat maps, `BasicColorFormatInfo` flags, **decode chains (esp. depth Y16 fixed/float)**, swizzle/Morton layout math | `pgraph/texture.c`, `swizzle.c`, vk maps | The shadow double-decode; texel-size ≠ semantics; quadrant nesting |
| B4 | **pfifo / command processing / present** — pusher, method dispatch, nondraw commands, begin/end renderpass discipline, finish reasons, submit/fence machinery, flip & vblank | `pfifo.c`, `vk/draw.c`, `vk/renderer.*` | The SB present-wall campaign; renderpass-compat fact; pass-break costs |
| B5 | **VK image/layout model** — the transition pair table, **tracked layouts + rest-layout invariant + GENERAL/feedback variants** (our addition, now load-bearing) | `vk/image.c`, `surface.c` helpers | v8 boot assert; the whole layout choreography |
| B6 | **Input stack** — xid devices (gamepad, **Steel Battalion controller**), keyboard scancode maps, binding resolution, hotplug | `xid*.c`, `ui/xemu-input.c`, `config_spec.yml` input section | The scancode-18 collision; defaults-vs-overrides confusion; "PRESS MAIN WEAPON BUTTON" dead-end |
| B7 | **Config system** — `config_spec.yml` → generated config, defaults/overrides, per-title practice | `config_spec.yml`, gen machinery | validation_layers unset = silently off; the SB toml serving LoC |
| B8 | **Snapshot/loadvm** — savevm/loadvm semantics, device vmstate, **hotplug interaction**, cross-build compatibility practice | QEMU core + xemu devices | CLI `-loadvm` vs hotplugged SBC; snapshot-as-benchmark methodology |
| B9 | **GL renderer** — the parallel backend (parity target for default-path claims) | `pgraph/gl/` (~6200 lines) | possibly_dirty hash-leak parity fix came from comparing backends |

### Tier C — inherited, operated (component cards)

`mcpx/apu` + DSP (audio; the dsp56300 prebuilt-blob dependency), `nvnet` +
`xblc` (network/comms), the SMBus device family (`smbus_*.c`, EEPROM
generation, SMC), `xbox.c`/`xbox_pci.c`/ACPI/LPC (machine model), `chihiro*`
(arcade variant), `ui/xui/*` scenes/menus/snapshot-manager (the frontend),
kernel shim/bootrom interface. Roughly 15 cards.

---

## 3. The spec template (Tier A/B)

Every spec is a **behavioral contract**, not a code tour. Sections, in order:

```markdown
# Spec — <feature>            (tier, code files, spec owner, last-verified date)

## Purpose & scope            what it does, what it deliberately does not
## External interface         env flags / config keys / registers / API calls /
                              monitor commands — every way in or out
## State & data structures    the structs that matter and WHAT EACH FIELD MEANS
                              (e.g. surface->size = rows × pitch; draw_time
                              advances per draw, frame_time per flip)
## INVARIANTS                 ★ the section that pays rent. Numbered, testable
                              statements. e.g. "resident surfaces never overlap
                              — UNLESS alias mode, where…"; "surface images
                              rest in ATTACHMENT except…"
## Policies & decision points every branch that chooses a path (compat checks,
                              eligibility gates, eviction rules) with WHY
## Failure modes              what breaks when invariants are violated; known
                              asserts; the traps already hit (link lessons)
## Instrumentation            counters/dumps that observe this feature; how to
                              tell it's working from a log
## Verified-by                the experiments/gates backing each claim
## Confidence & unknowns      per-claim tags (see §4) + open questions
```

**Confidence tags** — mandatory on every invariant/policy claim in Tier B:

- `[VERIFIED]` — backed by a cited experiment, gate, or in-tree assert
- `[DERIVED]` — read from code, logic traced, but never exercised deliberately
- `[ASSUMED]` — believed, unproven; a landmine with a flag on it

The shadow bug was an `[ASSUMED]` ("bit-identical") that got treated as
`[VERIFIED]`. The tags exist so that can't happen silently again.

Tier C cards: `Purpose / Interfaces / Invariants-we-rely-on / Danger notes /
Escalation trigger` — one page max.

---

## 4. Method: how a spec gets written (the campaign method, formalized)

1. **Read** the code; draft every invariant/policy you believe.
2. **Interrogate** the draft: for each claim, design the cheapest observation
   that could falsify it (a counter, a log line, a deliberate violation, a
   gate run). The perf-bench harness (T3) and the surface inspector (T10) are
   the workhorses here.
3. **Run** the observations; tag every claim VERIFIED/DERIVED/ASSUMED with the
   evidence cited inline.
4. **Adversarial pass**: someone (or a second agent) tries to construct a
   scenario the spec doesn't cover — every "what if" that survives goes into
   *Unknowns* rather than being silently resolved by optimism.
5. Land the spec; wire the **living rule** (§5).

The LoC campaign was accidentally steps 1–3 done under fire. Doing them
*before* the fire is the entire point of this plan.

---

## 5. Keeping specs alive (the part that usually fails)

- **The spec-with-change rule**: any PR touching a specced area updates the
  spec's affected sections *in the same PR* — reviewers treat a code change
  with an untouched spec the way they'd treat a code change with failing
  tests. The v9 arc would have rewritten B5's invariants three times; that
  churn is the documentation *working*, not overhead.
- **`docs/internal/SPECS.md` index**: one row per spec — tier, status
  (missing / draft / verified / stale), last-verified date, owner. The
  stale-marking is manual but the index makes staleness visible.
- **Verified-by links must be runnable**: a spec claim citing a gate cites
  the actual script/invocation, so re-verification is one command.
- Specs live in the **private** repo only (`docs/internal/`), reviewed like
  code, plain markdown. **Never on the public-merge allowlist** — see the
  banner at the top of this file and `docs/internal/README.md`.

---

## 6. Prioritized build order (pain-driven, honestly sized)

Sizing basis: B1/B2-scale subsystems took roughly a day of forced RE *each*
during the campaign while also firefighting; writing the spec calmly from the
now-fresh knowledge is faster for those two specifically, slower for the ones
we haven't fought in yet.

**Phase 1 — harvest the campaign while it's hot (highest value, lowest cost):**

| Spec | Size | Why now |
|---|---|---|
| B1 surface cache | 1 day | Freshest knowledge in the org; half the invariants already written in lessons/docs, need consolidation + tagging |
| B2 texture cache + S2T paths | 1 day | Same; the five paths' eligibility gates are already documented as code comments + campaign docs |
| B5 layout model | ½ day | Our own addition, load-bearing, zero prior art |
| B3 format model | ½ day | Small surface, highest bug-severity-per-line (the decode-chain lesson) |
| A2 perf lever reference | ½ day | Consolidate the flag semantics scattered across campaign docs into one contract page per flag |

**Phase 2 — the operating knowledge:**

| Spec | Size |
|---|---|
| B6 input/SBC (+ fix the scancode-18 collision as a verified-by) | ½ day |
| B7 config system | ½ day |
| B8 snapshots/loadvm | ½ day |
| B4 pfifo/present (large; the SB campaign docs carry much of it) | 1–1½ days |
| A3 instrumentation reference | ½ day |
| A1 VR subsystem (consolidate the extensive existing docs into contract form) | 1 day |

**Phase 3 — coverage:**

| Spec | Size |
|---|---|
| A4–A6 (overlay, game hacks, harness) | 1 day total |
| B9 GL renderer parity notes | ½ day |
| ~15 Tier-C cards | 1–2 days total (30–60 min each) |

**Total: ~9–11 focused days**, front-loaded so the first two days capture the
subsystems where we have *current* expert knowledge that will otherwise decay.

**First action item:** write **B1 (surface cache)** as the exemplar that sets
the bar — it exercises every template section, its invariants are the ones
that cost the most this campaign, and its verified-by hooks (T3 bench + T10
inspector + the SB counter gate) are exactly the tools already specced in
`tool-specs.md`. The two plans reinforce: tools verify specs, specs define
what the tools assert.

---

## 7. What this plan is *not*

- Not a QEMU documentation project (Tier D excluded by design).
- Not doc-comment coverage or API-doc generation — behavioral contracts only.
- Not a rewrite of the existing campaign docs — those are *evidence and
  narrative*; specs cite them rather than duplicate them.
- Not blocking: campaigns keep running; the spec-with-change rule simply makes
  each one leave the written record it currently leaves only in commit
  messages and lucky memories.
