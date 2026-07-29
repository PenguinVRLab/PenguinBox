# Licensing

PenguinBox is a fork of [xemu](https://xemu.app), which is a fork of [QEMU](https://www.qemu.org). **The aggregate is
GPL v2**, inherited from QEMU; see `LICENSE` and `COPYING`, and the SPDX header on each file.

## Per-file licensing

| Path | Licence | Note |
|---|---|---|
| `hw/xbox/nv2a/pgraph/vk/vr_*.{c,h}` | MIT | written for this fork |
| `ui/xui/sbc-overlay.{cc,hh}` | MIT | written for this fork |
| `hw/xbox/xid-steel-battalion.c` | LGPL-2.0-or-later | derived from `hw/xbox/xid.c`; carries the copyrights of espes, Jannik Vogel and Matt Borgerson, and keeps their licence |
| everything else | as stated in the file | upstream QEMU / xemu files keep their own licences |

MIT and LGPL-2.0-or-later both combine cleanly into a GPL v2 aggregate. No GPL v3 code is incorporated.

## The VR sources

The VR sources are original works of their sole author. They follow the design of a VR layer the same author wrote for
another emulator, but they are an independent implementation in a different language against a different renderer, and
contain no code from that project or from any third party. They are licensed here under the terms in their own SPDX
headers.

## Modified files

In the published tree, comments have been removed from source files, including files that came from QEMU and xemu. Every
copyright, licence and attribution notice is kept. The full upstream sources, with their original comments, are available
from the QEMU and xemu projects.

## VR profile data

Per-game VR profiles are data the emulator reads, not part of the emulator, and are licensed separately. Profiles published
under `vr-profiles/public/` are licensed CC BY-NC 4.0, with commercial rights reserved; see `vr-profiles/LICENSE.md` beside
them.

---

*This is a statement of licensing, not legal advice.*
