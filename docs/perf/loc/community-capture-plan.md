# Line of Contact — community capture plan

**Problem.** LoC's remaining perf wall needs measuring, and we cannot play it
here. Diagnosis has to come from someone else's session, which means the build
must produce a **self-explaining artefact** from a player who is not debugging
and should not have to.

Screenshots of the Video Debug HUD are the current instrument. They are one
frame, hand-cropped, unlabelled, and unusable for A/B.

---

## 0. What we already know, from one screenshot

```
MSPF 152 (≈6.6 fps)
QUEUE_SUBMIT_AUX      33     ← full vkQueueWaitIdle GPU drains / frame
  QUEUE_SUBMIT_1      13     ← surface DOWNLOAD  (GPU→CPU readback)
  QUEUE_SUBMIT_2       9     ← surface upload
  QUEUE_SUBMIT_4       5     ← texture upload
  QUEUE_SUBMIT_5       6     ← display
FINISH_SURFACE_DOWN    9     ← of 15 blocking finishes
SURF_DOWNLOAD         13
SURF_TO_TEX          238  /  SURF_TO_TEX_FALLBACK 3
FINISH_NEED_BUFFER_SPACE 0
```

**Readback dominates.** 13 GPU→CPU surface downloads per frame, each draining
the pipeline. The surf2tex work is confirmed doing its job (238 binds, 3
fallbacks), so this is the *next* wall, not a regression of it.

**Not yet known:** what those 13 downloads are. `RADAR_SCAN_SKIP: 0` is **not**
evidence — `XEMU_RADAR_SKIP_N` unset means N=0 means OFF, so that counter reads
zero on every title including SB1. It says nothing about whether LoC matched the
title gate.

## 1. Port the self-logging summary (exists, wrong branch)

`sb-graphics-research:hw/xbox/nv2a/pgraph/profile.c` already emits, every 60
frames, one stderr line of `mspf` plus every non-zero counter. That turns any
plain run into a diagnosable log with no HUD and no screenshots.

Port it, with three changes the community case demands:

| change | why |
|---|---|
| **`XEMU_PROF_INTERVAL`** (default 60) | At 6.6 fps, 1-in-60 is **a sample every 9 seconds**. Far too coarse to catch a transient stall. |
| **Spike trigger** — also emit when `mspf` exceeds a threshold (`XEMU_PROF_SPIKE_MS`) | The 1-in-60 sample is a *sample*, not an average. The frames we care about are exactly the ones it misses. |
| **Header line at startup** | Build commit, active renderer flags, title ID, radar-skip state. Without it a returned log cannot be attributed to a build or an arm. |

The existing `n < sizeof(buf) - 48` append guard must survive the port — a
truncated line silently reads as a zero counter (M-3).

## 2. One-command capture for a non-expert

The player should run **one thing**, play normally, and send **one file**. No
flags to remember, no HUD, no interpretation.

```
capture-loc.sh            # or a .bat for Windows
```

It must:

1. Launch LoC with logging enabled and stderr redirected to a dated file.
2. Write the header (§1) as the first line.
3. On exit, print **one sentence**: the file path and "please send this".
4. Produce a **single** artefact — not a directory, not a pile.

**Windows matters here.** The confirmed-working platform is the `.exe`, so the
capture path must exist there, not only on Linux. A `.bat` that sets the env
vars and redirects is enough.

## 3. Scene labelling — solve it by not requiring it

M-8 says an unlabelled timing is noise. But a player cannot be asked to annotate
while playing, and asking them to remember timestamps will fail.

**The counters label themselves.** A frame with `SURF_DOWNLOAD 13` and
`QUEUE_SUBMIT_AUX 33` is identifiable without being told what was on screen.
Cluster the log by counter signature and the scenes separate on their own.

Optional, if a volunteer is willing: a hotkey that stamps `MARK <n>` into the
log. Useful, never required — the analysis must work without it.

## 4. The A/B that actually answers the open question

Two arms, same session, same player, interleaved (M-8 — cross-boot comparisons
are noise):

| arm | env |
|---|---|
| **A** | current defaults |
| **B** | `XEMU_RADAR_SKIP_N=2` |

**What each outcome means:**

- **Log says `radar-skip: ... disabled`** → LoC failed the title gate
  (`SB_TITLE_ID 0x43430002`). The hack is SB1-only and needs LoC's title ID plus
  its four scanner EIPs. That is a RAM hunt, same shape as the OFP camera work.
- **Engages and `SURF_DOWNLOAD` falls** → LoC's readbacks are the same scanner
  class. The existing machinery applies and only the data is missing.
- **Engages and nothing changes** → different readback source entirely. Look at
  the cockpit monitors and any render-to-texture the guest reads back.

This is one env var and costs the volunteer nothing.

## 5. Constraints carried

- **The readback drives game logic.** SB1's radar surface feeds contacts and
  lock-on, so PCSX2's "just skip the download" dial is not available to us. Any
  fix must stay correct, or be a *cadence* trade (scan 1 frame in N) like the
  existing hack — which is why that hack is a cadence and not an off switch.
- **The skip works by rewriting guest EIP** to emulate a `ret`. Valid only
  because SB1's scanners are verified zero-arg `cdecl` ending in `0xC3`. LoC's
  equivalents need the same verification, not the assumption.
- **H-11:** we are outside the launch window. A build carrying this goes
  derive → fork → PR → merge → release, not a direct upload.

## 6. Order

1. Port §1 with the interval + spike + header changes.
2. §2 capture script, **Windows first** — that is the platform with a confirmed
   player.
3. Hand to a volunteer with the §4 A/B as two runs.
4. Analyse; only then decide whether the LoC scanner hunt is worth it.

Nothing here needs the volunteer to understand any of it, which is the point.
