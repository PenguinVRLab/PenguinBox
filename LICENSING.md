# LICENSING.md — provenance determination for the VR code

**Recorded 2026-07-26.** Written to be publishable: the point is that anyone who
notices the licence difference between this repo and its sibling can read the
reasoning instead of guessing at it.

---

## The question this answers

PenguinBox's VR sources (`hw/xbox/nv2a/pgraph/vk/vr_*.{c,h}`) descend, by design,
from the VR layer in the sibling PS2 core (`pcsx2-VR`, `pcsx2/VR/*`). **Those
sibling files are tagged `SPDX-License-Identifier: GPL-3.0`. These files are
not.** Read cold, that transition looks like GPLv3 code being relicensed — which
would be a licence violation and is exactly what this fork's **GPLv2 wall**
forbids (`RELEASING.md` §2: *"No GPLv3 code may ever flow in — not from the PS2
core, not from anywhere."*).

It is not a violation. Here is why, and here is the evidence.

## The facts

**1. The PS2 core's VR layer is the work of a single author, who is also this
project's author.**

```
$ git -C pcsx2-VR log --format='%an <%ae>' -- pcsx2/VR/ | sort | uniq -c
     47 Patrick Carey <patrickfcarey@gmail.com>
```

47 commits, spanning 2026-07-10 → 2026-07-20. **No other contributor has ever
touched that directory.** The only copyright line appearing in any of those
files is `SPDX-FileCopyrightText: 2026 Patrick Carey`.

**2. No third-party code was incorporated into those files.** They were written
*against* PCSX2's APIs, not by copying PCSX2's source into them. Calling into a
host program's interfaces does not place that program's text inside your file.

**3. Therefore the `GPL-3.0` tag on the sibling files is an authored choice, not
an inherited obligation.** It was selected to sit comfortably inside PCSX2's
GPLv3 aggregate. A sole copyright holder may license their own work under
different terms in a different project; that is the ordinary operation of
copyright, not an exception to it.

**4. The PenguinBox implementation is in any case an independent
reimplementation, not a copy.** It targets a different renderer (`pgraph_vk`
rather than PCSX2's Vulkan layer) in a different language (C, not C++).
Measured:

| PenguinBox | lines | pcsx2-VR | lines |
|---|---|---|---|
| `vr_manager.c` | 190 | `VRManager.cpp` | 568 |
| `vr_camera.c` | 555 | `CameraDriver.cpp` | 1454 |
| `vr_xr_session.c` | 492 | `XRSession.cpp` | 471 |

`vr_manager.c` and `VRManager.cpp` share **zero** function identifiers. What
travelled between the projects is design and method — which `RELEASING.md` §2
permits explicitly (*"Methods and documents are fine; code is not"*).

## Determination

**The VR sources in this repository are original works of their sole copyright
holder, licensed here under the terms stated in their own SPDX headers. No
GPLv3-licensed code from PCSX2, from `pcsx2-VR`, or from any third party is
incorporated in them.** The GPLv2 wall is intact.

## Two things this record exists to catch

**The derive's SPDX gate cannot detect this class of problem.** That gate fails
the build if a file under our VR paths carries a `GPL-3.0` tag. Our files carry
`LGPL-2.0-or-later`, so the gate reads clean — it would have passed a genuine
violation here just as happily as it passes this legitimate one. **The gate
checks labels; only this record checks provenance.** If VR code is ever brought
in from a sibling again, the determination must be re-made by hand — the
pipeline will not do it for you.

**Direction of travel is one-way.** Code originating here may move outward to
siblings. Code may not move inward from any GPLv3 sibling unless its copyright
holder is us and the same determination is recorded again.

## Per-file licensing

| Path | Licence | Why |
|---|---|---|
| `hw/xbox/nv2a/pgraph/vk/vr_*.{c,h}` | **MIT** | wholly authored here; chosen 2026-07-26 so the code stays portable to any sibling regardless of that sibling's licence — the exact situation that prompted this record |
| `tools/vr/hunt_scan.py`, `test_hunt_scan.py` | MIT | wholly authored here |
| `ui/xui/sbc-overlay.{cc,hh}` | MIT | wholly authored here |
| `hw/xbox/xid-steel-battalion.c` | **LGPL-2.0-or-later — forced** | derives from `hw/xbox/xid.c` and carries the copyrights of espes, Jannik Vogel and Matt Borgerson. **This one is not ours to relicense**, and `RELEASING.md` §2's blanket "MIT for every VR file we authored" does not apply to it. |
| everything else | upstream QEMU / xemu | unchanged; upstream files ship their headers and comments intact |

## Profile data is NOT covered by any of the above

**The per-game VR profiles are separately licensed and are not open source.**
See [vr-profiles/LICENSE.md](vr-profiles/LICENSE.md). This follows the model
already established in the PS2 core.

The reasoning: a profile is **data the emulator reads**, not part of the
emulator — like a config file, a save, or a texture pack. It is also the
expensive part of this project. Each profile is the output of a per-title RAM
hunt: locating camera state in guest memory and establishing an anchor that
survives a reboot. **That work is not recoverable from the source, and no
licence on the program conveys any right to it.**

| | licence |
|---|---|
| `vr-profiles/public/` | CC BY-NC 4.0 — **commercial rights expressly reserved** |
| `vr-profiles/private/` | **proprietary, all rights reserved. Never ships.** |

Enforced mechanically, in two places: `private/` is absent from
`tools/release/allowlist.txt`, and `tools/release/derive.py::profile_guard`
aborts the run if anything under it reaches the output. The same guard refuses
to ship `public/` **without** `LICENSE.md` beside it — otherwise the profiles
would land in the public tree appearing to inherit the emulator's licence.

**State as of 2026-07-26:** PenguinBox has no profile loader yet, and ships
**zero** profile data. The head-look mechanism exists in `config_spec.yml` with
every address key defaulting to `''`. The existing per-title results live only
in `docs/` and `tools/vr/`, both excluded from the allowlist — verified. The
folder and this licence exist **before** the first profile ships, not after.

The aggregate remains **GPLv2** by QEMU inheritance. MIT and LGPL-2.0-or-later
both flow into a GPLv2 aggregate cleanly; GPL-3.0 does not, which is the whole
reason the wall exists.

---

*This is a record of authorship facts and the reasoning drawn from them. It is
not legal advice. The facts in §"The facts" are verifiable from the repositories
named.*
