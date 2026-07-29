# Linux packaging — AppImage today, Flatpak later

**Written 2026-07-27**, after two shipped-artifact defects in one day. Both came
from the same root cause, and Flatpak deletes that cause. This records why, so
the decision is not re-derived later.

---

## The root cause behind both of today's bugs

**The build machine was an undeclared dependency.**

| defect | mechanism |
|---|---|
| **USB passthrough missing from the AppImage** | `meson_options.txt:199` declares `option('libusb', 'auto')`. `auto` means *silently degrade*. The rig had no `libusb-1.0` dev headers, so configure dropped passthrough and **the build still succeeded**. The Windows `.exe` had it, because the MXE cross image happens to carry the dependency. Two binaries on one release, differing in a feature, silently. |
| **AppImage will not start on most distros** | An AppImage bundles some libraries but still links the **host's glibc**. Built on the rig (Ubuntu 24.04) it demands `GLIBC_2.39` and refuses to start on Ubuntu 22.04, Mint 21.x or Debian 12 — the error is `version 'GLIBC_2.39' not found`. |

Neither is a coding error. Both are *packaging* errors, and both reduce to
"whatever happened to be installed on the machine that ran the build."

## What we did about it (AppImage-era mitigations)

- `build.sh` now passes **`--enable-libusb`** unconditionally, converting a
  silent omission into a hard configure failure.
- `build-release.sh` asserts the artifact after the fact: `ldd` the binary for
  libusb, then `ls` the AppDir to confirm it was actually bundled. Three checks
  on one feature, because passthrough went missing **twice, by two different
  mechanisms** — `auto` at configure, then linuxdeploy's excludelist at bundle.
- The glibc floor is **documented in the release notes**, not fixed.

An audit of the remaining `auto` features found libusb was the only critical one
at risk: `opengl`, `sdl` and `slirp` are already `value: 'enabled'` upstream
(hard-fail), and Vulkan has no option at all. Audio looked alarming
(`ALSA: NO`, `PulseAudio: NO`) but is fine — SDL3 vendors its own headers and
**dlopens** `libpulse.so.0` / `libpipewire-0.3.so.0` / `libasound.so.2` at
runtime, so it needs no dev packages and soft-fails per backend.

## Why Flatpak fixes this at the root

The manifest (`flatpak/app.xemu.xemu.yml`) already has the important parts:

```yaml
runtime: org.freedesktop.Platform
runtime-version: '25.08'      # pins the ENTIRE userspace, including glibc
sdk: org.freedesktop.Sdk
finish-args:
  - --device=all              # USB passthrough needs this
```

1. **No glibc floor.** The runtime supplies its own glibc, so the host's version
   is irrelevant. The problem does not exist rather than being worked around.
2. **The build machine stops being a variable.** `flatpak-builder` compiles
   inside the SDK sandbox against *declared, pinned* dependencies — the manifest
   already pins e.g. `libpcap 1.10.6` by URL. A missing host header cannot
   silently change what ships.
3. **`--device=all` is already declared.** Flatpak sandboxes device access, so a
   manifest lacking this would break USB passthrough outright. Ours grants
   `/dev/bus/usb`.

## Caveats — what Flatpak does NOT fix

- **Host udev permissions still apply.** The sandbox grants the device *node*; a
  user without rights on the controller fails exactly as they would natively.
  The udev-rules helper is still needed. See [features/sbc-analog-input.md](features/sbc-analog-input.md).
- **The GPU driver still comes from the host** via runtime extensions
  (`org.freedesktop.Platform.GL`). Correct behaviour, but driver-specific issues
  stay host-dependent.
- **Packaging does not fix source bugs.** The duplicated `xemu_vr_frame()` call
  and the OHCI single-packet limit would have shipped in a Flatpak identically.
- **`--device=all` is broad.** Justified for an emulator doing USB passthrough,
  but expect questions if this ever goes to Flathub.

## Decision (2026-07-27)

**Flatpak is out of scope for this release.** Recorded consequence: the
**Ubuntu 22.04 AppImage rebuild is a stopgap for a problem Flatpak deletes
entirely.** Before spending build cycles on the downgrade, weigh it against
simply shipping the Flatpak.

A 22.04 build container was prepared and is kept at
[`Containerfile.appimage22`](../Containerfile.appimage22) if the stopgap is
wanted. Risk assessment for that downgrade:

| area | verdict |
|---|---|
| **USB passthrough** | **No risk.** libusb 1.0.25 vs 1.0.27 is API-stable, and the older bundled copy runs on *more* hosts. `usb-host` and the OHCI fix are ours, base-independent. |
| **Vulkan** | **The only real risk.** `meson.build:2393` uses `dependency('vulkan')` — the host's, not vendored. Our renderer needs `VK_API_VERSION_1_3` plus `VK_EXT_custom_border_color`, `VK_EXT_debug_utils`, `VK_EXT_memory_budget`, `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore_fd`. 22.04 ships headers 1.3.204, which has all of them — should build, but verify rather than assume. |
| **Audio** | Minor. 22.04's pipewire is 0.3.48; SDL3 may build without that backend. Pulse and ALSA remain, and they are dlopened. |
| **Compiler** | GCC 11/12 instead of 13 — laxer, not breaking. Note the opposite direction bites: the cross toolchain's GCC 16 *errors* where 13 only warns, which already broke one Windows build. |

### DECIDED 2026-07-27: the 22.04 downgrade will NOT be done

Owner's call, and the reasoning is sound: this is a **beta**, people are already
running it, and rebuilding to widen distro reach spends build cycles on a problem
Flatpak removes outright. Ship the 24.04 AppImage with its floor **documented in
the release notes** (already done) and put the effort into the Flatpak instead.

Consequence to accept knowingly: **the AppImage requires Ubuntu 24.04 or newer.**
Anyone on 22.04, Mint 21.x or Debian 12 sees `version 'GLIBC_2.39' not found`
and cannot run it. That is the expected answer to such a report — it is not a
regression to investigate.

*If this is ever revisited:* build on 22.04, diff the configure summary against
the 24.04 build line by line, and adopt only if nothing regressed. Keep the
working 24.04 AppImage shipped until the replacement is proven equal or better.

## The general rule this earns

> **A packaged artifact's feature set must be asserted, not assumed.** Anything
> `auto` in `meson_options.txt` is a silent-degrade candidate; anything taken
> from the host base (glibc, Vulkan) is a portability floor. Check the artifact,
> not the repo — documenting a capability is a claim about the *binary*.
