# KVM for xemu — feasibility study and wiring plan

**Status: STUDY (2026-07-26).** Nothing built. Written against
`origin/steel-battalion` @ `e92be7f6ca`, as the Phase-2 "structural bet" spike
requested by [`docs/perf-roadmap-30-to-60.md`](perf-roadmap-30-to-60.md)
(sb-graphics-research lane). Deliverable is a **verdict and a wiring plan**, not
a promise — per that roadmap's own framing.

**Context — the wall it would demolish.** The other lane measured the scene as
**guest-CPU-bound: TCG thread ~94% busy ≈ 31 ms of executed guest instructions
per frame**, needing **~1.85×** to reach 60. KVM would execute that instruction
stream *natively* instead of translating it.

---

## 0. Verdict

> **⚠️ SUPERSEDED FOR STEEL BATTALION (2026-07-26).** Phase 0 measured the scene
> as **GPU-emulation-bound via a 77.5% guest spin-wait**, not CPU-logic-bound
> (`sb-graphics-research` @ `3193e49a83`). The 31 ms this study is built on is
> mostly a kernel busy-wait on the emulated GPU, so native execution would just
> spin faster against the same bottleneck. **KVM is deflated for SB.** The
> MMIO question (§3) is answered — `MMIO_DISPATCH = 1302/frame`, which would
> have *passed* my gate. §1 and §2 remain verified and reusable for any title
> that profiles as genuinely CPU-logic-bound. See `kvm-agent-brief.md`.
>
> **Possible salvage worth one check:** the new #1 lever is *spin-detect-and-
> yield*, which under KVM is a **hardware** feature — AMD pause-filter / Intel
> PLE, surfaced via `kvm_vcpu_on_spin()`. **But it only triggers on `PAUSE`
> instructions**, and P3-era Xbox kernel code may spin without one. If that loop
> does contain `PAUSE`, KVM gets the #1 lever for free; if not, it is irrelevant.
> Cheap to check: disassemble `0x80024a96-0x80024b81` and look.

**More feasible than expected.** The dirty-tracking bridge I assumed would be
the blocker turns out to be **already plumbed end-to-end** — only a sync
*trigger* is missing (§2). The real risk is elsewhere: KVM **inverts** the cost
of MMIO, and xemu's NV2A is MMIO-driven. **Whether KVM wins is
decided by one number nobody has measured yet, and it is measurable today, under
TCG, without writing any KVM code.** That measurement is the first move.

---

## 1. Green lights (verified in-tree)

| fact | evidence |
|---|---|
| KVM is a supported accelerator for our target | `meson.build:299-301` — `kvm_targets = ['i386-softmmu', 'x86_64-softmmu']` on x86_64 hosts |
| The accelerator code is present | `accel/kvm/kvm-all.c`, `accel/kvm/kvm-accel-ops.c` |
| The rig can run it | `/dev/kvm` present; CPU reports `svm` (AMD-V) |
| The machine model is **not** TCG-bound | `hw/xbox/xbox.c` has exactly **one** `tcg_enabled()` guard (`:274`), and it is benign — x87 FERR IRQ registration, standard PC-machine behaviour, a TCG-only *feature* not a TCG *requirement* |
| The machine already has KVM-aware paths | `hw/xbox/xbox.c:282` — `kvm_pit_in_kernel()` branch, inherited from QEMU's PC machine |
| Guest RAM is an ordinary RAM MemoryRegion | `hw/xbox/xbox.c:194` — `memory_region_init_ram(ram, NULL, "xbox.ram", …)`; KVM maps ordinary RAM as a memslot with no special handling |
| The guest CPU is a subset of the host | Xbox = Coppermine P3/Celeron (x86-32, MMX/SSE). Every feature is a strict subset of any modern x86-64, so CPUID masking is expressible. 32-bit guest on 64-bit host is fully supported by KVM |

**xemu is TCG-only by omission, not by design.** Nobody built the path; nothing
structurally forbids it.

---

## 2. The dirty-tracking question — mostly ALREADY WIRED (corrected)

My first pass called this "THE blocker." **Reading the code disproved that.**
Recording the correction because the difference is a month of work.

### What is at stake
xemu adds two custom dirty-memory clients (`include/exec/ramlist.h`):
```c
#define DIRTY_MEMORY_NV2A      3
#define DIRTY_MEMORY_NV2A_TEX  4
```
Under TCG these are set by the store path (`accel/tcg/cputlb.c`) via
`DIRTY_CLIENTS_ALL`. Under KVM `cputlb.c` never runs, so the naive fear is that
the bits are never set → textures render permanently stale.

### But the KVM path is already plumbed — three links, all present

| link | evidence |
|---|---|
| **1. xemu REQUESTS logging** on VRAM for both clients | `hw/xbox/nv2a/nv2a.c:231-232` — `memory_region_set_log(d->vram, true, DIRTY_MEMORY_NV2A)` and `…NV2A_TEX` |
| **2. KVM ENABLES logging for any requesting client** | `accel/kvm/kvm-all.c:662` — `if (memory_region_get_dirty_log_mask(mr) != 0)`. Not migration-only; **any** client arms the memslot |
| **3. The KVM sync SETS the NV2A bits** | `kvm_slot_sync_dirty_pages()` (`kvm-all.c:757-763`) → `physical_memory_set_dirty_lebitmap()` (`system/physmem.c:1347`), which explicitly ORs `DIRTY_MEMORY_NV2A` **and** `DIRTY_MEMORY_NV2A_TEX` alongside VGA/MIGRATION |

Someone already extended the KVM dirty-log sync to carry the NV2A clients. The
mechanism is **not missing**.

### The actual remaining gap: nobody TRIGGERS the sync

`grep memory_region_sync_dirty_bitmap hw/xbox/nv2a/` → **nothing**.

Under TCG the bits appear continuously, so no sync is needed and none was ever
written. Under KVM the bits only reach QEMU's bitmap after a `KVM_GET_DIRTY_LOG`
ioctl — so `check_texture_dirty()` would consult a bitmap nobody populated.

**⇒ The fix is a sync call at the right cadence, not a subsystem rewrite.**

### The real design question — cadence vs. intra-frame correctness
- **Per frame** (cheap, natural): one ioctl per frame. But NV2A checks dirtiness
  **per texture bind**, so a CPU write early in frame N would not be visible
  until frame N+1 — a one-frame staleness window that may or may not matter.
- **Per dirty-check** (correct, expensive): an ioctl per texture bind is
  certainly too often — at the ~4,600 binds/frame the other lane measured, this
  is absurd.
- **Likely answer:** per frame, plus `KVM_CAP_MANUAL_DIRTY_LOG_PROTECT` (present
  in this tree) to control re-protection explicitly, and the **dirty ring**
  (`kvm-all.c:604`) which reports per-vCPU without a full-slot scan.

**And the lucky alignment still holds:** `check_texture_dirty()` is already
page-granular (`TARGET_PAGE_ALIGN` / `TARGET_PAGE_MASK`) — exactly KVM dirty
logging's granularity. There is no semantic mismatch to bridge.

**Cost model.** One VM exit per page per protect-cycle (not per write). A
640×480×32bpp framebuffer ≈ 1.2 MB ≈ **300 pages → ~300 exits/frame** ≈
**0.3–0.6 ms** against a 31 ms budget. Affordable.

**Still owed the same treatment:** `DIRTY_MEMORY_NV2A` (surfaces) and the
**#2514** mem-callback premise — both assume guest writes reach QEMU (see R5).

## 3. The inversion nobody has flagged — MMIO gets *worse*

This is the finding that decides the whole bet, and it cuts against KVM.

| access | under TCG | under KVM |
|---|---|---|
| guest RAM read/write | inline, TLB-checked | **native, free** ✅ |
| **MMIO to NV2A registers** | a C function call | **a VM exit → userspace → device model** ❌ |

TCG makes MMIO *cheap* (it is already in the interpreter's world). KVM makes it
**expensive** — a full world switch, ~1–2 µs each.

xemu's NV2A is register-driven: pushbuffer PUT/GET pointers, method kicks,
register pokes. The perf roadmap already lists *"(f) MMIO trap overhead
(pushbuffer kicks, register pokes)"* among the 31 ms, and notes **~2,100 draws +
~1,000 constant-uploads per frame**.

**The arithmetic that decides it:**

| MMIO accesses / frame | KVM exit cost @1.5 µs | verdict |
|---|---|---|
| 500 | 0.75 ms | irrelevant — KVM wins big |
| 5,000 | 7.5 ms | serious tax, still likely a win |
| **20,000** | **30 ms** | **KVM is a WASH or a LOSS** |

**⇒ The single most valuable experiment costs nothing and needs no KVM code:
count MMIO accesses per frame under TCG today.** If the number is small, KVM is
extremely attractive. If it is large, the bet is dead before any integration work
— and we would have learned that in an afternoon instead of a month.

---

## 4. Remaining risk register

**R1 — Timing fidelity (highest residual risk).** Under KVM the guest runs at
host speed, not 733 MHz. Faster is the *goal*, but Xbox titles target fixed
hardware and may calibrate against it. Mitigating factors: the game almost
certainly paces on vblank/timers driven by our NV2A, which we still control; and
the roadmap already identifies **spin-waits** as a cost term — those complete
faster rather than breaking. Watch for: audio pitch/rate, physics steps, anything
delay-loop calibrated. QEMU offers **no cycle throttling under KVM** — pacing must
come from the device timing we already own.

**R2 — Memory type / coherency.** VRAM is typically write-combining. NV2A reads
guest memory through a host pointer (`d->vram_ptr`). Under KVM, guest WC writes
and QEMU's view need correct ordering, or the GPU samples stale bytes. TCG hides
this entirely (all writes pass through QEMU). Needs explicit validation.

**R3 — SMM / MTRR / cache-mode quirks.** The MCPX boot ROM and BIOS may use
modes KVM handles differently. Unknown until booted; the boot path is the natural
first smoke test.

**R4 — Threading.** Under KVM the vCPU runs inside `KVM_RUN` on its own thread,
dropping the BQL — structurally the same model as MTTCG. Our VR pfifo thread
already drops and retakes the BQL, and the XR pacer is independent. **Expected
compatible**, but the VR path has never run against a non-TCG vCPU.

**R5 — The #2514 interaction.** That port exists so guest DMA over a live surface
triggers download/invalidate instead of silently scribbling. Its premise is that
guest writes reach QEMU. Under KVM they do not. **Whatever fix serves §2a must
also serve #2514, or the #1238 texture crash returns** — the exact bug that was
blocking gameplay.

---

## 5. Staged spike plan (each stage cheap, decisive, and pre-registered)

**Ordered so the cheapest kill-shots come first.**

**S0 — Count MMIO accesses per frame (TCG, no KVM code).** Instrument the NV2A
MMIO read/write handlers with a per-frame counter; run the measured mission scene.
→ **KILL CONDITION: >20,000 MMIO accesses/frame ⇒ abandon KVM**, because exit
overhead alone would consume the entire frame budget. *Half a day.*

**S1 — Boot smoke test.** `-accel kvm` on the existing binary, expect breakage;
record *where* it dies (BIOS? MCPX? first NV2A access?). This costs nothing and
maps R3 immediately. Rendering will be wrong — that is expected, §2 predicts it.
*Hours.*

**S2 — Dirty-tracking bridge.** Enable KVM dirty logging (prefer the dirty ring)
on the RAM memslot; drive `DIRTY_MEMORY_NV2A*` from the sync instead of the store
path; sync before `check_texture_dirty()` consults. → **Success = textures update
correctly under KVM.** This is the bulk of the engineering. *Days.*

**S3 — Measure the actual guest-CPU collapse.** Re-run the profiler. Did 31 ms
fall, and by how much after MMIO exits and dirty-log exits are paid?
→ **KILL CONDITION: <1.85× on the guest thread ⇒ the bet failed**, since that is
the roadmap's stated requirement for 60.

**S4 — Correctness gauntlet.** #1238 repro, save states (vmstate under KVM is a
separate question), audio pitch, timing-sensitive scenes, and a VR session (R4).

**Per M-1**, each kill condition above is written **before** running the test.
**Per M-4**, KVM ships default-OFF behind `-accel` selection, which it already is
by construction.

---

## 6. What I would tell the roadmap

The roadmap calls KVM *"the nuclear option… if viable, this is THE answer and
everything in Phase 1 becomes moot."* Two refinements from this study:

1. **It is not all-or-nothing, and the deciding factor is MMIO density, not
   translation cost.** S0 answers that for ~half a day of work, with no KVM
   integration — it should run *before* Phase 1's incremental levers, because a
   green S0 changes what is worth optimising at all.
2. **The dirty-tracking rewrite is not a side quest — it is the project.** And it
   lands in the *same* subsystem the current perf campaign is already rebuilding
   (`check_texture_dirty` / `mark_textures_possibly_dirty` / the #2514 callbacks).
   There is a real opportunity to design that seam **once**, so it serves TCG
   today and KVM later, instead of twice.

**Honest ceiling caveat.** Even a perfect KVM path leaves the pfifo/render thread
(~51%) and the GPU (50–72%) untouched. KVM demolishes the *guest-CPU* wall; it
does not make the renderer faster. The roadmap's Phase-3 reprojection route
remains the orthogonal comfort win, and stays ours.
