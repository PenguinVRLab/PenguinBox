# vr-profiles — per-game VR profile data

**Read [LICENSE.md](LICENSE.md) first.** These files are *data*, separately
licensed from the emulator. `public/` is CC BY-NC 4.0 with commercial rights
reserved; `private/` is proprietary and never ships.

## Layout

| path | ships? | licence |
|---|---|---|
| `LICENSE.md` | **yes** — with `public/` | — |
| `public/` | **yes** | CC BY-NC 4.0, commercial rights reserved |
| `private/` | **never** | proprietary, all rights reserved |

`private/` is excluded from `tools/release/allowlist.txt`, and `derive.py` aborts
if anything under it reaches the output — belt and braces, because a profile is
the one artefact here that cannot be re-derived from the source.

## Current state (2026-07-26) — honest version

**PenguinBox has no profile loader yet.** Unlike the PS2 core, which reads
sidecar YAML through a profile database, head-look configuration here is a set of
knobs in `config_spec.yml`:

```
headlook_enable / headlook_sensitivity / headlook_invert_pitch
headlook_engine_period
headlook_yaw_addr / headlook_pitch_addr / headlook_gate_addr
headlook_gate_value / headlook_gate_mask
headlook_base_mode / headlook_base_ptr / headlook_base_offset
```

**Every one of those ships with `default: ''` or `0`.** The emulator ships the
*mechanism* with **no profile data in it** — which is the correct split, and it
is why the existing per-title work is not currently exposed. Today a profile is
applied by writing those keys into a user's config.

**This folder exists now so the rights position is established before the first
profile ships, not after.** When a loader lands, per-game files go here and the
licence above already covers them.

## Why the split exists

A profile is the durable output of a RAM hunt: the camera-state anchor for one
title, stable across reboots. Finding one is the expensive part of this project —
far more so than the code that consumes it. The emulator is open; the data is
the author's. That distinction is the whole reason this folder is licensed
separately.
