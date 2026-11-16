# Attributions

PenguinBox is a fork of [xemu](https://xemu.app), itself a fork of QEMU. The
overwhelming majority of this software is the work of the QEMU and xemu projects
and their contributors, credited in the per-file copyright headers and in
MAINTAINERS.

This file additionally credits **individual upstream contributors whose xemu
pull requests this fork incorporates.** Several of these PRs are still open
against upstream; we ported them onto our branch, and their authorship belongs
here in the canonical place so it is not lost to a squashed or re-authored
commit. Everything below is incorporated under xemu's existing GPL-2.0 /
LGPL-2.0-or-later licensing — it is other people's open-source work, credited,
not claimed as original to this fork beyond the changes we made on top.

## Fred Hallock (faha223) &lt;specialfred453@gmail.com&gt;

- **Steel Battalion Controller device emulation** — xemu PR #1803, *"UI: Device
  emulation - Steel Battalion Controller"* (2024) —
  https://github.com/xemu-project/xemu/pull/1803

  Our Steel Battalion controller support derives directly and substantially from
  this PR. `hw/xbox/xid-steel-battalion.c` was **created** in it (our copy is
  ~83% unchanged); the controller input handling in `ui/xemu-input.{c,h}`, the
  on-screen controller mask in `ui/xui/gl-helpers.cc`, `data/sb_controller_mask.png`,
  and the changes to `hw/xbox/xid.{c,h}`, `ui/xui/input-manager.cc`,
  `ui/xui/main-menu.cc`, `ui/xui/main.cc` and `ui/xemu.c` are all from this PR
  (several files byte-identical). What this fork adds on top: the analog /
  latched-switch and unbind-safety fixes, the cockpit overlay, the sim registry
  and pre-flight gate, the diagnostics panel, and the VR panel.

- **OHCI per-endpoint packet fix** — xemu PR #1531 (2023) —
  https://github.com/xemu-project/xemu/pull/1531
  The base of our OHCI fix (landed as `usb(ohci): … port faha223's ohci_fix`).

## Erik Abair (abaire) &lt;erik.abair@gmail.com&gt;

- **NV2A DMA access routed through memory callbacks** — xemu PR #2514, *"nv2a:
  Route DMA access through mem callbacks"* (2025) —
  https://github.com/xemu-project/xemu/pull/2514
  The surface-sync memory-callback mechanism our NV2A read/performance work
  builds on derives from this PR.

## On git authorship

Where these ports entered our history through squashed or re-authored commits,
the authorship recorded above is the authoritative statement of who wrote the
underlying code. Corrections and additions to this file are welcome — if you are
an upstream author whose work is here and the credit is wrong or missing, please
open an issue.
