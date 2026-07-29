# KVM spike — agent brief

**You are picking up a research spike.** Read this page, then
[`kvm-feasibility-study.md`](kvm-feasibility-study.md). Everything else is
optional until you need it.

**Branch:** `kvm-feasibility` (this one). **Base:** `steel-battalion` @ `e92be7f6ca`.

> ## ⚠️ SUPERSEDED FOR STEEL BATTALION — read this first
>
> **Phase 0 measured (2026-07-26, `sb-graphics-research` @ `3193e49a83`) and the
> premise of this spike is wrong for SB.** The scene is **GPU-emulation-bound via
> a guest SPIN-WAIT**, not CPU-logic-bound:
> - **77.5% of guest execution is a spin** — one kernel busy-wait at EIPs
>   `0x80024a96-0x80024b81` (~64% across four hot addresses), waiting on the GPU.
> - `pfifo_work ≈ 39 ms` of a 50 ms frame — **the GPU-emulation thread is the
>   real bottleneck**; the guest burns cycles spinning on it.
> - Real game logic + D3D driver work is a **minority** of guest execution.
>
> **⇒ KVM does not help SB.** Native execution speeds up the ~22% of real logic;
> the guest would then spin **natively and faster** waiting on the *same-speed*
> emulated GPU. My study assumed those 31 ms were productive work. They mostly
> are not.
>
> **My S0 is also already answered:** `MMIO_DISPATCH = 1302/frame` — comfortably
> inside my "KVM wins" band (~2 ms of exits). MMIO was never the killer; the
> spin was. *Do not re-run S0 for SB.*
>
> **What survives:** KVM deflation is **title-specific** — the measuring agent
> says so explicitly (*"HL2 may genuinely differ; measure per-title, never
> assume"*). A title that is genuinely CPU-logic-bound would still benefit, and
> §1–§2 of the study (KVM is a supported target; the NV2A dirty bridge is already
> plumbed and only lacks a sync trigger) remain **verified and reusable**.
>
> **Before reviving this spike for any title, run the Phase-0 profiler first and
> confirm a low spin fraction.** That is now the entry gate, replacing S0.

---

## 1. The mission in one paragraph

Steel Battalion runs at ~30 FPS and the scene is **guest-CPU-bound**: the TCG
thread is ~94% busy ≈ **31 ms of executed guest instructions per frame**, and we
need that under **16.7 ms** (a **~1.85×** speedup) for 60. The Xbox CPU is a
standard x86 (Coppermine P3) and our host is x86-64, so **KVM could execute that
instruction stream natively** instead of translating it — collapsing the wall for
every game, not just this one. xemu is TCG-only today because nobody built the
path, **not** because anything forbids it. Your job is a **feasibility verdict
with evidence**, not a working KVM port.

## 2. State — five things that are already true

1. **KVM is a supported accelerator for our target.** `meson.build:299-301`
   (`kvm_targets` includes `i386-softmmu`), `accel/kvm/` present, rig has
   `/dev/kvm` + AMD-V.
2. **The Xbox machine model is not TCG-bound.** Exactly one `tcg_enabled()` guard
   (`hw/xbox/xbox.c:274`, benign x87 FERR), and it already has a
   `kvm_pit_in_kernel()` branch at `:282`.
3. **The NV2A dirty-tracking bridge is ALREADY PLUMBED** — this was the expected
   blocker and it is not one. See study §2 for the three links. **The only gap is
   that nothing calls `memory_region_sync_dirty_bitmap()` from `hw/xbox/nv2a/`.**
4. **The real risk is MMIO.** KVM *inverts* MMIO cost: cheap under TCG (a
   function call), expensive under KVM (a VM exit, ~1–2 µs). NV2A is
   register-driven.
5. **Nothing has been built.** The study is static analysis only.

## 3. Your first move — S0, and do not skip it

**Count MMIO accesses per frame, under TCG, with no KVM code.**

Instrument the NV2A MMIO read/write handlers with a per-frame counter and run the
measured mission scene. This is half a day and it decides the entire bet:

| MMIO / frame | KVM exit cost @1.5 µs | verdict |
|---|---|---|
| 500 | 0.75 ms | KVM wins big |
| 5,000 | 7.5 ms | taxed, still likely a win |
| **>20,000** | **>30 ms** | **KILL — abandon KVM** |

**That kill condition is pre-registered (M-1). Write your result down before
interpreting it.** A red S0 saves a month; a green S0 should change what the
Phase-1 incremental levers are even worth doing.

## 4. Code map — the file:line that matter

| what | where |
|---|---|
| The wall, measured | `docs/perf-roadmap-30-to-60.md` (on `sb-graphics-research`) |
| Custom dirty clients | `include/exec/ramlist.h:14-15` |
| xemu requests VRAM logging | `hw/xbox/nv2a/nv2a.c:231-232` |
| KVM enables it for any client | `accel/kvm/kvm-all.c:662` |
| KVM sync sets the NV2A bits | `kvm-all.c:757-763` → `system/physmem.c:1347` |
| Dirty ring (better than full-slot scan) | `kvm-all.c:604` |
| The consumer, page-granular | `hw/xbox/nv2a/pgraph/vk/texture.c:446` (`check_texture_dirty`) |
| Dirty contagion across the cache | `texture.c:429` (`mark_textures_possibly_dirty`) |
| TCG's store path (what KVM replaces) | `accel/tcg/cputlb.c` |
| Xbox RAM creation | `hw/xbox/xbox.c:194` |

## 5. Rules that will bite you

- **M-6 — process checks use `ps -eo comm` prefix-match, NEVER `pgrep -f`** with
  a string that appears in your own command line. *(This caught me: two of three
  "live PIDs" were my own grep self-matching.)*
- **M-7 — one rig owner at a time.** The perf-campaign agent on
  `sb-graphics-research` currently owns the rig. **Get an explicit hand-off
  before building or measuring there.**
- **M-1 — one measurement "suggests", two independent runs "establish."** Every
  claim ships with a pre-registered falsification test.
- **M-8 — label the scene.** Cross-boot timings are noise; only interleaved
  same-boot arms support timing conclusions.
- **H-1 — commit as `Patrick Carey <patrickfcarey@gmail.com>`, both author AND
  committer, no `Co-Authored-By`, never any AI attribution.**
- **H-2 — live-session check before any rig GPU/VR action.** Three emulators
  share one headset.
- **Stale-binary trap:** builds happen **on the rig from its own checkout**.
  Confirm the binary mtime moved before trusting any result.

## 6. What would make this fail

- **S0 comes back >20k MMIO/frame** → the exit tax eats the win. Stop.
- **S3 measures <1.85× on the guest thread** → the bet failed on its own terms.
- **Timing fidelity (R1)** — KVM has **no cycle throttling**; the guest runs at
  host speed. Pacing must come from the device timing we own. Watch audio pitch,
  physics steps, delay-calibrated loops.
- **The #2514 interaction (R5)** — the DMA/mem-callback port assumes guest writes
  reach QEMU. Whatever serves the dirty sync must also serve #2514, **or the
  #1238 texture crash returns** — the bug that was blocking gameplay in the first
  place. This is the one I'd least want to be wrong about.

## 7. Scope discipline

Deliver a **verdict with evidence**, in this order: S0 (MMIO count) → S1 (boot
smoke under `-accel kvm`, expect breakage, record *where*) → S2 (sync trigger +
cadence) → S3 (measure) → S4 (correctness gauntlet). Study §5 has each stage with
its kill condition.

**Do not** start with S2 because it is the interesting engineering. S0 is
half a day and can end the project.

**Lane hygiene:** the perf campaign owns `sb-graphics-research`; Steel Battalion
input/VR work owns `steel-battalion`. Keep KVM work here.
