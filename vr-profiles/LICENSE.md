# VR profile licensing

## These files are DATA, not part of the emulator

The per-game VR profiles in this folder are original works of **Patrick Carey**
(<patrickfcarey@gmail.com>), authored from scratch. They are tuning and
measurement data that the emulator *reads at runtime* — they are **not** program
code, **not** derived from xemu or QEMU, and **not** derived from any game. Like
a configuration file, a save file, or a texture pack, each profile is a separate
work that happens to be consumed by the emulator: the emulator's licence covers
the *program*, not this *data*.

Each profile is the output of a per-title reverse-engineering effort — locating
the camera state in guest RAM and establishing an anchor that survives a reboot.
The addresses and constants in these files are the result of that work. They are
not discoverable from the emulator's source, and nothing in the emulator's
licence conveys any right to them.

## Licence for the shipped profiles

Copyright © 2026 Patrick Carey. All rights reserved except as granted here.

The profiles shipped in `public/` are licensed under **Creative Commons
Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)** —
<https://creativecommons.org/licenses/by-nc/4.0/>. You may **use, share, and
adapt** them for **non-commercial** purposes, with attribution.

**Commercial rights are expressly reserved.** Any commercial use — selling a
profile, bundling one into a paid product or release, or distributing profiles
for commercial advantage — requires a **separate written licence** from the
copyright holder. Enquiries: <patrickfcarey@gmail.com>.

## Extended / subscriber profiles (distributed separately)

Profiles under `private/`, and any profile distributed outside the `public/` base
set, are **proprietary**: copyright © Patrick Carey, all rights reserved,
licensed to the recipient for personal use only under their own distribution
terms, **no redistribution**.

`private/` **never crosses into the public tree.** This is enforced mechanically
by the release process. See
[../LICENSING.md](../LICENSING.md).

---

*Nothing here alters the licence of the emulator's program code, which remains
GPLv2 in aggregate (QEMU inheritance), with our own VR sources under MIT. This
file governs only the profile data in this folder.*
