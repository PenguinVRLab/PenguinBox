# The pfifo wall — four-agent synthesis (2026-07-26)

**Context.** Steel Battalion ~30 FPS, 50 ms frame, `pfifo_work ≈ 39 ms`. Guest
spins 77.5% waiting on pfifo. **Spin-yield was tried: saved power, no FPS gain** —
so pfifo is not starved of a core. Four Opus agents were run on non-overlapping
solution spaces (decompose / parallelize / emit-less / prior-art). This is the
synthesis. **All source-read, none measured on the rig** — the confirming reads
are listed in §2 and cost nothing.

> **Picking this up as the perf-campaign agent? Read
> [`pfifo-wall-agent-brief.md`](pfifo-wall-agent-brief.md) instead** — same
> findings, ordered as a hand-off, with every file:line re-verified in-tree and a
> §9 separating what is confirmed from what is still just a lead. This document is
> the reasoning behind it.

---

## 1. The number that reframes the problem

> **39 ms ÷ 2,100 draws = 18.6 µs per draw.**

Calibration from prior art:
- **Dolphin's documented *worst case*** — Twilight Princess, 27,000 draws/frame,
  22 fps — is **1.7 µs/draw** of total frame time.
- **PCSX2** calls ~1,000 draws/frame "a good limit", 2,250 "Ouch".

At 2,100 draws we are **not** in unusual-workload territory. We are **~11× over
budget per draw.** That rules out "the interpreter is slow" as the primary
explanation, and two projects independently agree: RPCS3 found optimizing the
FIFO decoder *"was not actually necessary… far more important to improve how the
draw calls are processed"*; PPSSPP profiles display-list processing at **~4%**.

## 2. THE UNANIMOUS FIRST MOVE — all four agents, independently

**`pfifo_work = mspf − pfifo_idle`, and `pfifo_idle` counts only time parked on
the fifo condvar.** Therefore **every blocking GPU wait is counted as "work"** —
`vkWaitForFences`, `vkQueueWaitIdle`, present.

**The 39 ms is NOT established to be CPU cycles.** "39 ms of CPU burn" and
"20 ms CPU + 19 ms blocked on a fence" both predict a 50–72%-busy GPU. Spin-yield
proved pfifo owns a core; it did **not** prove pfifo is *computing*.

**Corroborating evidence for "mostly blocked":** the ~44 ms floor is
**scene-independent** (a community empty-grassland scene also sits at 19 FPS ≈
44 ms with almost no draws). **Per-draw CPU work cannot produce a constant floor.
A fixed number of drains can.**

### Do this first — half a day, zero code
Every counter is already wired into **Debug → Video Debug** (`ui/xui/debug.cc`):

| read | decides |
|---|---|
| **`QUEUE_SUBMIT_AUX`** | count of `vkQueueWaitIdle` **full GPU drains/frame** — *the single most diagnostic number in the build* |
| `QUEUE_SUBMIT` + `QUEUE_SUBMIT_1…5` | blocking finishes, and which subsystem caused each (1 surf-download, 2 surf-upload, 3 surf-create, **4 texture upload**, 5 display) |
| **`FINISH_NEED_BUFFER_SPACE`** | descriptor/UBO exhaustion stalls |
| `SHADER_UBO_DIRTY` vs `NOTDIRTY` | per-draw uniform waste |
| `PIPELINE_NOTDIRTY` / `PIPELINE_RENDERPASSES` | pipeline + render-pass churn |
| `TEX_HASH`, `TEX_HASH_KB` *(sb-graphics-research only)* | texture-hash cost |

Plus **two A/B runs**: `display.renderer = NULL` (if `pfifo_work` collapses, the
cost is the Vulkan backend, not method decode) and **Vulkan vs OpenGL**.

**Pre-registered kill criterion (agent 2):** if `pfifo_cpu` < 12 ms of the 39,
**abandon the thread-offload direction entirely.**

## 3. The mechanism — xemu's Vulkan backend has ZERO CPU/GPU overlap

| | command buffers in flight |
|---|---|
| Dolphin | **8** (+ pools that *grow* rather than stall) |
| PCSX2 | **3** |
| RPCS3 | multiple |
| **xemu** | **1** (+1 aux) |

- **`command.c:104`** — `pgraph_vk_end_single_time_commands()` = `vkQueueSubmit` +
  **`vkQueueWaitIdle`**: a full queue drain. Called from texture upload, surface
  create/upload/download, display. **Every texture upload idles the entire GPU.**
- **`draw.c:1213-1290`** — `pgraph_vk_finish()` = submit + unconditional
  `vkWaitForFences(…, UINT64_MAX)`.
- **Estimated 8–10 full drains per frame**, plus a synchronous wait per upload.

**The control experiment is already in the tree:** the **OpenGL backend has
exactly ONE `glFinish()`** (at flip stall); texture uploads are bare
`glTexImage2D` with no sync. Hence upstream **xemu issue #1838 — ~17 titles where
Vulkan is *slower* than OpenGL.** This is a known upstream problem, not a mystery.

> ### ⚠️ Fork-specific amplification — ours, and it makes this worse
> **`vr_xr_compositor.c:976` — `c->queue = r->queue`: the VR compositor shares
> the graphics queue.** Every `vkQueueWaitIdle` above also drains our XR layer
> copies and the SBC panel. Upstream does not have this. **Fixing the drains pays
> double for us.**

## 4. The one-line bug — pure waste, verified against the GL backend

**The VK texture path never clears `possibly_dirty` on the cache-HIT path.** It
returns at `vk/texture.c:1196` with the flag still set; the only clear is at
`:1203`, on the **miss** path. **GL clears it correctly and unconditionally**
(`gl/texture.c:374`). Upstream's bug, not a fork regression.

Consequences:
- A texture flagged once **re-hashes on every bind for the life of its cache
  node**; the flagged set only grows.
- `pgraph_vk_flush` marks *all of VRAM* possibly-dirty (`renderer.c:95`) — **one
  flush permanently poisons all 1024 cache entries.**
- **Retrodicts two unexplained results:** why exact-dirty narrowing measured
  −60% on cutscenes but ~0% in mission (narrowing the *marking* cannot un-set
  stuck flags), and why `TEX_HASH=310` with `TEX_HASH_KB≈35,000` (avg 113 KB —
  the sticky set is the *large* textures).

**Fix: move `snode->possibly_dirty = false;` above the `if (binding_found)`
block.** Cheapest experiment in the whole report.

## 5. Ranked plan (converged across agents)

**Tier 1 — remove synchronization stalls. Days-to-weeks, zero accuracy cost.**

1. **Kill `vkQueueWaitIdle` in upload/download** → fence-tracked deferred
   completion. Reference: PCSX2 `GSDownloadTextureVK` (records + fence stamp,
   never waits; three-tier `Flush()`), Dolphin `WaitForFenceCounter(u64)`.
   **Note: commit `8f9124f90f` already does part of this and lives only on
   `origin/sb-graphics-research`** — landing it is free progress.
2. **Fix descriptor management** — `UNIFORM_BUFFER_DYNAMIC` + dynamic offsets,
   drop `FREE_DESCRIPTOR_SET_BIT`, per-frame pools that grow. Today: 1024 sets
   and an 8 MiB arena, and SB sits on **both** cliffs (~1,000 sets, ~6.6 MB) →
   `NEED_BUFFER_SPACE` → full GPU sync **mid-frame**. Evidence: Khronos measured
   **44 → 27 ms (−38%)** from exactly this; RPCS3 +10–15% and +10–20%.
   **⚠ Use dynamic UBOs, NOT SSBOs** — RPCS3 partially reverted SSBOs because
   *"especially NV hardware has really slow memory access when using SSBOs in
   fragment shaders"*, **and our GPU is an RTX 4050.**
   *(A 2-line capacity bump — 8192 sets / 32 MiB — is the cheap interim.)*
3. **Version the vertex RAM mirror** (`vertex.c:56`). mborgerson explicitly
   invited this on PR #2345; a one-line hack there measured **+50%** and was
   rejected on correctness grounds — **take the number as the size of the prize,
   not the fix.** Correct shape: Dolphin's `VKStreamBuffer` + `WaitForClearSpace`.
4. **Frames in flight** (2–3 command buffers). Of 14 `pgraph_vk_finish` sites,
   only ~4 are correctness-required; the rest are pool/ordering artifacts.
5. **Persist the pipeline cache** — upstream **PR #2799 is written, unmerged, and
   absent from our fork.** ~1–2 days to port. Fixes first-run stutter only.

**Tier 2 — reduce per-command CPU cost.** Per-draw uniform rebuild + hash
(`shaders.c:440`, its own `// FIXME: Dirty tracking`, ~17 KB/draw ≈ 36 MB/frame
answering a question `pg->vsh_constants_dirty[]` already answers); diff-based
method dispatch on a shadow register file (RPCS3 + PPSSPP converged on this
independently); the unconditional eviction drain at `surface.c:655`
(*"For now, finish to be safe"* — the one wall never narrowed); upstream
**PR #2930** (batch inline-array methods, *"several milliseconds per frame"*,
also absent from our fork).

**Tier 4 — threading. Last, if ever.** Dolphin's **Dual Core is now off by
default** and `bSyncGPU` *"failed for eight years"*. PCSX2's MTGS only works
because *"the GS almost never reads back"* — **the NV2A reads back constantly**.
RPCS3's Multithreaded RSX is **still off by default in 2026** and is an offload
thread, not a parallel FIFO. Realistic version is an **offload thread with a size
threshold**, not a command-processor split.

**Tier 5 — DO NOT.** Pushbuffer JIT / command-sequence cache: Dolphin's `DLCache`
is the only shipped instance in emulation history and was **deleted in 2014** —
*"caching display lists is doomed to fail"*, *"broke every second game and only
boost a bit"*. PPSSPP deleted its analogous vertex cache at **"<1%"** benefit.
Reused secondary command buffers: **zero adoption** across RPCS3/Xenia/Ryujinx.
GPU-side pushbuffer interpretation: nobody ships it.

## 6. The escape hatch other emulators used is closed to us

PCSX2's answer to the readback stall — structurally identical to SB's
render-then-CPU-scan radar — was a **4-mode `GSHWDownloadMode` user dial**, i.e. a
correctness/speed selector. **We cannot take that exit: the radar surface drives
game logic** (contact blips, lock-on). "Disable readback" is not a tradeoff here,
it is a broken game.

## 7. HLE — folklore corrected, verdict unchanged

**Don't.** But two corrections worth carrying:
- **Cxbx-Reloaded's LLE-GPU slowness was Windows SEH, not pushbuffer
  interpretation** — one context switch per GPU register access via guard-page
  exceptions. Their own FAQ: *"Its slowness is not inherent to LLE, but the way it
  is currently integrated."* **Do not cite it as evidence against LLE.**
- **There is no published Cxbx-R vs xemu benchmark on any title. None.** The HLE
  performance advantage is architecturally plausible and **entirely unmeasured**.
  Do not budget against it.

*(If it were ever revisited: only **~15 of 1,866** OOVPA signatures cover the
draw path — a genuinely small surface. Keep LLE authoritative, intercept only
`D3DDevice_Draw*`/`BeginPush`/`EndPush`, default everything off.)*

## 8. Caveats

- **Everything here is read from source; none of it is measured on the rig.**
  §2's counter read confirms or kills most of it in minutes.
- Agent 2's precedent items (Dolphin Dual Core, PCSX2 `GSHWDownloadMode`) arrived
  as **headline claims whose citations were lost** — re-verify before they enter
  a design doc.
- The `xbox-game-hacks` layer exists on `sb-graphics-research`, **not** on
  `steel-battalion`; one agent's "no precedent exists" is correct only for the
  branch it read.
