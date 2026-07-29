# The pfifo wall — agent brief

**You own the perf campaign on `sb-graphics-research`. This is a hand-off, not a
directive.** Read this page, then [`pfifo-wall-four-agent-synthesis.md`](pfifo-wall-four-agent-synthesis.md)
if you want the prior-art reasoning behind it.

**Source:** four Opus agents run on non-overlapping solution spaces
(decompose / parallelize / emit-less / prior-art) after your spin-yield result.
**Branch:** `kvm-feasibility`. Cherry-pick these two docs wherever you want them.

> **What your spin-yield result actually established.** Spin-yield saved power and
> gave no FPS. That proves the pfifo thread is **not starved of a core**. It does
> **not** prove pfifo is *computing*. That distinction is the whole brief.

---

## 0. TL;DR

1. **`pfifo_work` is not known to be CPU time.** Measure the split before
   optimizing anything. §2. Half a day, no code.
2. **A one-line bug makes every flagged texture re-hash forever.** §3. Verified
   in-tree against the GL backend. Cheapest experiment available.
3. **The VK backend has one command buffer and zero CPU/GPU overlap.** §4.
   Dolphin runs 8, PCSX2 runs 3.
4. **18.6 µs/draw means this is not a workload problem.** §1.
5. **Do not build a pushbuffer JIT or a parallel FIFO.** §7. Both have shipped
   precedents that failed.

## 1. The number that reframes the problem

> **39 ms ÷ 2,100 draws = 18.6 µs per draw.**

Calibration:
- **Dolphin's documented *worst* case** — Twilight Princess, 27,000 draws/frame,
  22 fps — is **1.7 µs/draw** of total frame time.
- **PCSX2** calls ~1,000 draws/frame "a good limit," 2,250 "Ouch."

At 2,100 draws SB is **not** an unusual workload. We are **~11× over budget per
draw**. That makes "the pushbuffer interpreter is slow" a poor primary
hypothesis, and two projects agree independently: RPCS3 found optimizing the FIFO
decoder *"was not actually necessary… far more important to improve how the draw
calls are processed"*; PPSSPP profiles display-list processing at **~4%**.

**Implication:** the cost is per-draw overhead or per-draw stalling, not decode.

## 2. DO THIS FIRST — the measurement, before any optimization

### Why

```
pfifo_work = mspf − pfifo_idle
```

…and **`pfifo_idle` counts only time parked on the fifo condvar.** So every
blocking GPU wait — `vkWaitForFences`, `vkQueueWaitIdle`, present — **is counted
as "work."**

Both of these fit the observed data equally well:
- 39 ms of CPU burn
- 20 ms CPU + 19 ms blocked on a fence

They imply completely different fixes. **Nothing measured so far distinguishes
them.**

**Corroborating evidence for "mostly blocked":** the ~44 ms floor is
**scene-independent** — the community's empty-grassland scene also sits at ~19 FPS
with almost no draws. **Per-draw CPU work cannot produce a scene-independent
floor. A fixed number of GPU drains can.**

### The reads — all counters already exist and are already wired to the UI

**Debug → Video Debug** (`ui/xui/debug.cc`), in the measured mission scene:

| counter | where | tells you |
|---|---|---|
| **`QUEUE_SUBMIT_AUX`** | `debug.h:83`, inc'd `command.c:103` | **count of full `vkQueueWaitIdle` GPU drains per frame — the single most diagnostic number in the build** |
| `QUEUE_SUBMIT` + `QUEUE_SUBMIT_1…5` | `draw.c:~1200` | blocking finishes, and which subsystem caused each (1 surf-download, 2 surf-upload, 3 surf-create, **4 texture upload**, 5 display) |
| **`FINISH_NEED_BUFFER_SPACE`** | `debug.h:75` | descriptor/UBO exhaustion → full GPU sync **mid-frame** |
| `SHADER_UBO_DIRTY` vs `_NOTDIRTY` | | per-draw uniform waste |
| `PIPELINE_NOTDIRTY` / `PIPELINE_RENDERPASSES` | | pipeline + render-pass churn |

Plus **two A/B runs, same boot, interleaved** (M-8):
- **`display.renderer = NULL`** — if `pfifo_work` collapses, the cost is in the
  Vulkan backend, **not** in method decode. This single run splits the problem in
  half.
- **Vulkan vs OpenGL** — see §4 for why this is the control experiment.

### Pre-registered kill criterion — write it down before you interpret (M-1)

> **If pfifo CPU time is under 12 ms of the 39, abandon the thread-offload
> direction entirely.** Threading a thread that is already blocked buys nothing.

## 3. The one-line bug — verified in-tree, costs only hashing

**VK never clears `possibly_dirty` on the cache-HIT path.**

`hw/xbox/nv2a/pgraph/vk/texture.c`:
```c
if (binding_found) {
    ...
    if (possibly_dirty && content_hash != snode->hash) {
        upload_texture_image(pg, texture_idx, snode);
        snode->hash = content_hash;
    }
    NV2A_VK_DGROUP_END();
    return;                       // <-- ~:1196, possibly_dirty still set
}

NV2A_VK_DPRINTF("Cache miss");
memcpy(&snode->key, &key, sizeof(key));
snode->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
snode->possibly_dirty = false;    // <-- :1203, MISS PATH ONLY
```

**GL clears it unconditionally** — `gl/texture.c:374`, `key_out->possibly_dirty =
false;`, reached on both hit and miss. Upstream's bug, not a fork regression.

### What it actually costs — be precise, this matters for what you expect

The stuck flag does **NOT** cause redundant uploads. The `content_hash !=
snode->hash` comparison still catches those. **It causes redundant *hashing*:**

```c
if (!surface_to_texture && possibly_dirty) {
    content_hash = fast_hash(texture_data, texture_length);   // every bind, forever
```

So a texture flagged once **re-hashes on every single bind for the life of its
cache node**, and the flagged set only ever grows. Since `pgraph_vk_flush` marks
*all* of VRAM possibly-dirty (`renderer.c:95`), **one flush permanently poisons
all 1024 cache entries.**

**Do not expect an upload-count change. Expect a hash-cost change.**

### Why this is worth doing first

It **retrodicts two results that currently have no explanation**:
- Exact-dirty narrowing measured **−60% on cutscenes but ~0% in mission** —
  narrowing the *marking* cannot un-set flags that are already stuck.
- `TEX_HASH = 310` with `TEX_HASH_KB ≈ 35,000` — avg **113 KB**, i.e. the sticky
  set is the *large* textures. ~35 MB of hashing per frame.

**Fix:** move `snode->possibly_dirty = false;` above the `if (binding_found)`
block. **Confirm against `TEX_HASH_KB`, which only exists on your branch** — it is
not on `steel-battalion`, which is why this went unnoticed.

## 4. The mechanism — zero CPU/GPU overlap

| | command buffers in flight |
|---|---|
| Dolphin | **8** (+ pools that *grow* rather than stall) |
| PCSX2 | **3** |
| RPCS3 | multiple |
| **xemu** | **1** (+1 aux) |

- **`vk/command.c:104`** — `pgraph_vk_end_single_time_commands()` does
  `vkQueueSubmit` + **`vkQueueWaitIdle(r->queue)`**: a full queue drain. Called
  from texture upload, surface create/upload/download, and display. **Every
  texture upload idles the entire GPU.** *(The only other `vkQueueWaitIdle` is
  `gpuprops.c:455`, init-time — ignore it.)*
- **`vk/draw.c:1213-1290`** — `pgraph_vk_finish()` = submit + unconditional
  `vkWaitForFences(…, UINT64_MAX)`.
- Estimated **8–10 full drains per frame** plus a synchronous wait per upload —
  **`QUEUE_SUBMIT_AUX` gives you the real number in one run.**

**The control experiment is already in the tree:** the **GL backend has exactly
one `glFinish()`** (at flip stall); its texture uploads are bare `glTexImage2D`
with no sync. Which is why upstream **issue #1838 lists ~17 titles where Vulkan is
*slower* than OpenGL.** This is a known upstream problem, not a mystery — and it
means the VK-vs-GL A/B in §2 is measuring exactly the thing in question.

### Fork-specific amplification — ours, and it is intentional

**`vr_xr_compositor.c:976` — `c->queue = r->queue;`** The VR compositor shares
pgraph's graphics queue. The design comments say so deliberately (`:27` *"Same
queue + same thread"*, `:347` *"The pfifo thread owns the graphics queue here"*),
and the compositor does its own fence sync (`c->fences[i]`, `c->panel_fence`).

**This is not a bug — but it means every `vkQueueWaitIdle` above also drains our
XR layer copies and the SBC panel.** Upstream doesn't carry this cost.
**Removing the drains pays double for us.** Do not "fix" the queue share; fix the
drains.

## 5. Ranked plan — *after* §2 comes back

**Tier 1 — remove synchronization stalls. Zero accuracy cost.**

1. **Kill `vkQueueWaitIdle` in upload/download** → fence-tracked deferred
   completion. Reference implementations: PCSX2 `GSDownloadTextureVK` (records +
   fence stamp, never waits; three-tier `Flush()`), Dolphin
   `WaitForFenceCounter(u64)`. **Commit `8f9124f90f` already does part of this and
   exists only on your branch** — landing it is free progress.
2. **Fix descriptor management** — `UNIFORM_BUFFER_DYNAMIC` + dynamic offsets,
   drop `FREE_DESCRIPTOR_SET_BIT`, per-frame pools that grow. Today it is
   `descriptor_sets[1024]` (`renderer.h:314`, `:369`) + an 8 MiB arena, and **SB
   sits on both cliffs** (~1,000 sets, ~6.6 MB) → `NEED_BUFFER_SPACE` → full GPU
   sync mid-frame. Six trigger sites: `draw.c:383`, `draw.c:1860`,
   `surface.c:176`, `texture.c:613`, `shaders.c:170`.
   Evidence: Khronos measured **44 → 27 ms (−38%)** from exactly this change;
   RPCS3 +10–15% and +10–20%.
   > **⚠ Use dynamic UBOs, NOT SSBOs.** RPCS3 partially reverted SSBOs because
   > *"especially NV hardware has really slow memory access when using SSBOs in
   > fragment shaders"* — **and the rig is an RTX 4050.**

   *A 2-line capacity bump (8192 sets / 32 MiB) is the cheap interim probe —
   if `FINISH_NEED_BUFFER_SPACE` is nonzero, do this before the real fix.*
3. **Version the vertex RAM mirror** (`vertex.c:56`). mborgerson explicitly
   invited this on PR #2345; a one-line hack there measured **+50%** and was
   rejected on correctness grounds. **Take the number as the size of the prize,
   not as the fix.** Correct shape: Dolphin's `VKStreamBuffer` +
   `WaitForClearSpace`.
4. **Frames in flight** (2–3 command buffers). Of 14 `pgraph_vk_finish` sites,
   only ~4 look correctness-required; the rest are pool/ordering artifacts.
5. **Persist the pipeline cache** — upstream **PR #2799 is written, unmerged, and
   absent from our fork.** ~1–2 days to port. Fixes first-run stutter only, not
   steady-state FPS.

**Tier 2 — reduce per-command CPU cost.**
Per-draw uniform rebuild + hash (`shaders.c:440`, carries its own
`// FIXME: Dirty tracking`, ~17 KB/draw ≈ 36 MB/frame — answering a question
`pg->vsh_constants_dirty[]` already answers but **nobody reads**); diff-based
method dispatch on a shadow register file (RPCS3 and PPSSPP converged on this
independently); the unconditional eviction drain at `surface.c:655`
(*"For now, finish to be safe"* — the one wall never narrowed); upstream
**PR #2930** (batch inline-array methods, *"several milliseconds per frame"*,
also absent from our fork).

**Tier 4 — threading. Last, if ever.** Dolphin's **Dual Core is now off by
default**; its `bSyncGPU` *"failed for eight years."* PCSX2's MTGS only works
because *"the GS almost never reads back"* — **the NV2A reads back constantly.**
RPCS3's Multithreaded RSX is **still off by default in 2026** and is an offload
thread, not a parallel FIFO. The realistic version is an **offload thread with a
size threshold**, not a command-processor split. **§2's kill criterion may
eliminate this tier outright.**

## 6. The escape hatch other emulators used is closed to us

PCSX2's answer to the readback stall — structurally identical to SB's
render-then-CPU-scan radar — was a **4-mode `GSHWDownloadMode` user dial**: a
correctness/speed selector. **We cannot take that exit. SB's radar surface drives
game logic** (contact blips, lock-on). "Disable readback" isn't a tradeoff here,
it's a broken game. Any readback fix has to be *correct*, not optional.

## 7. DO NOT — shipped precedents that failed

- **Pushbuffer JIT / command-sequence cache.** Dolphin's `DLCache` is the only
  shipped instance in emulation history and was **deleted in 2014** — *"caching
  display lists is doomed to fail,"* *"broke every second game and only boost a
  bit."* PPSSPP deleted its analogous vertex cache at **"<1%"** benefit.
- **Vulkan secondary command buffers reused across frames.** Zero adoption across
  RPCS3 / Xenia / Ryujinx.
- **GPU-side pushbuffer interpretation.** Nobody ships it.
- **Full D3D8 HLE.** Not worth it — but see §8 for a folklore correction.

## 8. Two folklore corrections worth carrying

- **Cxbx-Reloaded's LLE-GPU slowness was Windows SEH, not pushbuffer
  interpretation** — one context switch per GPU register access via guard-page
  exceptions. Their own FAQ: *"Its slowness is not inherent to LLE, but the way it
  is currently integrated."* **Do not cite Cxbx as evidence against LLE.**
- **No published Cxbx-R vs xemu benchmark exists on any title.** The HLE
  performance advantage is architecturally plausible and **entirely unmeasured.**
  Don't budget against it.

*(If HLE were ever revisited: only **~15 of 1,866** OOVPA signatures cover the
draw path — a genuinely small surface. Keep LLE authoritative, intercept only
`D3DDevice_Draw*` / `BeginPush` / `EndPush`, default everything off.)*

## 9. Verification status — read this before quoting anything

**Verified by me, in-tree, on `kvm-feasibility` (= `steel-battalion` base):**
`command.c:103-104` counter + drain · `gpuprops.c:455` is the only other
`vkQueueWaitIdle` · VK `texture.c` hit-path early return without clear, miss-path
clear at `:1203` · `gl/texture.c:374` unconditional clear · `descriptor_sets[1024]`
at `renderer.h:314`/`:369` · `NV2A_PROF_QUEUE_SUBMIT_AUX` at `debug.h:83` ·
`NV2A_PROF_FINISH_NEED_BUFFER_SPACE` at `debug.h:75` + its 6 trigger sites ·
`vr_xr_compositor.c:976` queue share.

**NOT verified — treat as leads:**
- **All performance numbers.** Nothing in this brief was measured on the rig.
  §2 confirms or kills most of it in one session.
- The **"8–10 drains/frame"** estimate — `QUEUE_SUBMIT_AUX` replaces it with fact.
- Agent 2's precedent claims (**Dolphin Dual Core off-by-default**, **PCSX2
  `GSHWDownloadMode`**, **Khronos 44→27 ms**, **RPCS3 SSBO revert**) arrived as
  headline claims **whose citations were lost in the report.** Re-verify before
  any of them enters a design doc or a commit message.
- **Line numbers drift between branches.** Everything above is `steel-battalion`-
  based; `sb-graphics-research` has diverged. Grep, don't trust the line number.
- `TEX_HASH` / `TEX_HASH_KB` exist **only on `sb-graphics-research`** — confirmed
  absent from `steel-battalion`.
- One agent reported "no game-hacks precedent exists." That is true **only of the
  branch it read** — `xbox-game-hacks` is on your branch.

## 10. Rules that will bite you

- **M-1** — one measurement *suggests*, two independent runs *establish*. Every
  claim ships with a pre-registered falsification test. §2's is written above;
  record your result **before** interpreting it.
- **M-6** — process checks use `ps -eo comm` prefix-match, **never `pgrep -f`**
  with a string that appears in your own command line. *(This caught me: two of
  three "live PIDs" were my own grep self-matching.)*
- **M-7** — **one rig owner at a time. You own it.** I have not touched it and
  will not without an explicit hand-off.
- **M-8** — label the scene. Cross-boot timings are noise; only interleaved
  same-boot arms support timing conclusions. Both A/Bs in §2 must be interleaved.
- **H-1** — commit as `Patrick Carey <patrickfcarey@gmail.com>`, **both** author
  AND committer, **no `Co-Authored-By`**, never any AI attribution.
- **H-2** — live-session check before any rig GPU/VR action; three emulators share
  one headset.
- **H-3** — force the VULKAN renderer. A failed VK init **silently falls back to
  OpenGL** with only a notification. If you run the §2 VK-vs-GL A/B, **confirm
  which renderer actually initialized** — otherwise you may measure GL twice.
- **Stale-binary trap** — builds happen **on the rig from its own checkout**.
  Confirm the binary mtime moved before trusting any result. A test on
  yesterday's binary "disproves" code it never contained.
- **ISS-B15** — the film grain **stays**. The user likes it. The removal patch is
  a **measurement tool only**, never a shipped fix.
