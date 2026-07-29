# Spec — Build & release harness

> **PRIVATE — internal spec. Never merged to the public repo; see [README](README.md).**

| Tier | Code | Status | Last verified |
|---|---|---|---|
| A | `build.sh`, `build-release.sh`, `ubuntu-win64-cross/`, `flatpak/app.xemu.xemu.yml` | v1 draft | 2026-07-27 |

## Purpose & scope

One reproducible entry point per distributable, plus the dev build, with the
hard-won fixes baked in: `./build.sh` (the configure/make wrapper every path
runs through), `./build-release.sh windows|linux` (release builds), the MXE
cross-toolchain container (`ubuntu-win64-cross/`), and the flatpak manifest.

Out of scope: the **AppImage recipe** — the artifact actually shipped for
Linux is built *outside* this harness (the G6 gap, handed to the packaging
agent; `docs/perf/tooling-gaps.md` G6); CI workflows (the fork deleted them);
the inherited macOS path (never exercised by this fork).

## External interface

- `./build.sh [-j N] [--debug] [-p <platform>] [-a <arch>] [extra args…]` —
  trailing args pass through verbatim to `./configure` → meson
  (`build.sh:262-268`). Always sets `-DXBOX=1`, `--target-list=i386-softmmu`,
  `--enable-renderdoc`. Platforms: `Linux` (default → `package_linux`),
  `Darwin`, MSYS-native Windows, `win64-cross` (container-only:
  `--cross-prefix=$CROSSPREFIX --static`, target `qemu-system-i386w.exe`).
- **`--enable-libusb` semantics**: `build.sh` does not define this flag — it
  is a configure passthrough mapped by `scripts/meson-buildoptions.sh:391`
  to `-Dlibusb=enabled`, which flips the meson dependency from
  optional-silent to **required-loud** (see invariants 1-3). As of
  2026-07-27 `build.sh` does **not** force it (recommended fix in
  `docs/perf/loc/HANDOFF-packaging.md`, not yet applied).
- `./build-release.sh windows [--rebuild-image] [--no-strip]` — podman
  (docker fallback), image `xemu-win64-cross:latest` from
  `ubuntu-win64-cross/gcc.Dockerfile`, runs `./build.sh -p win64-cross`
  in-container, then strips.
- `./build-release.sh linux` — flatpak-builder (host binary, or the
  `org.flatpak.Builder` flatpak, auto-installed `--user`, no sudo) against
  `flatpak/app.xemu.xemu.yml`, then `flatpak build-bundle`. Does **not** use
  the container image; the freedesktop SDK sandbox supplies all deps.

## Artifact inventory (what each path produces)

| Path | Artifact | Notes |
|---|---|---|
| `./build.sh` (Linux) | `build/qemu-system-i386` → `dist/xemu` + `LICENSE.txt` | The rig dev binary. The stale-binary trap lives here: confirm mtime newer than the change before trusting any rig result. |
| `./build.sh -p win64-cross` (in image) | `build/qemu-system-i386w.exe` → `dist/xemu.exe` + `LICENSE.txt` | Static mingw-w64 exe. |
| `./build-release.sh windows` | `dist/PenguinBox-win64.exe` | ~32 MB stripped (208 MB `--no-strip`). Bakes four fixes: `-p win64-cross`, pre-seeded dsp56300 blob (`subprojects/dsp56300/dsp56300-0.1.3-x86_64-pc-windows-gnu.tar.gz` — the image lacks the `curl` program), `TAR_OPTIONS=--no-same-owner` (rootless-podman chown), strip pass. |
| `./build-release.sh linux` | `dist/PenguinBox-x86_64.flatpak` | app-id kept `app.xemu.xemu` deliberately (rebrand pending; collides with Flathub xemu). freedesktop 25.08; builds THIS working tree via `type: dir` (skips build/dist/.git/.flatpak-builder). **Not the shipped artifact** — see invariant 4. |
| *(outside the harness)* | `PenguinBox-x86_64.AppImage` | **The artifact actually attached to releases**, built by hand from upstream's `build-linux.yml` recipe (deb → AppDir → `linuxdeploy --output appimage`). Provenance not reproducible from this repo (G6). |
| `ubuntu-win64-cross/gcc.Dockerfile` | image `xemu-win64-cross:latest` | ubuntu:24.04 + MXE (pinned d9441093), **`plugins/gcc16`** → GCC 16, target `x86_64-w64-mingw32.static`; fork-local `.mk` overrides (vulkan-headers, glib, sdl2, libsamplerate, libressl, curl); builds **libusb1** into the toolchain. |
| *User-supplied, never built, never committed* | MCPX boot ROM `mcpx_1.0.bin` (512 B, sha1 `5d270675b54eb8071b480e42d22a3015ac211cef`); flash BIOS `xbox-4627_debug.bin` (1 MB, sha1 `a5503364b2b07b9bb01cb6ef2fb412c25364bb7d`) | Loaded via `bootrom_path`/`flashrom_path`. Hashes-only policy (invariant 6). |

## Invariants

1. [VERIFIED] **meson's libusb is `feature: 'auto'`** (`meson_options.txt:199`)
   and the lookup is `dependency('libusb-1.0',
   required: get_option('libusb').enabled(), …)` (`meson.build:2223-2227`) —
   so with the default, a missing `libusb-1.0` dev package **silently drops
   USB passthrough** (`CONFIG_USB_LIBUSB` unset, `meson.build:2723`) and the
   build still succeeds. Field-verified the expensive way: a community
   member's source build had passthrough, our packaged build didn't
   (`docs/perf/loc/HANDOFF-packaging.md`). Passthrough is how a **real Steel
   Battalion controller** is used — this fork's flagship peripheral.
2. [DERIVED] **`--enable-libusb` fails loudly**: it maps to
   `-Dlibusb=enabled`, making the dependency `required: true`, so configure
   aborts if the dev package is absent. Release builds MUST pass it; the
   loud-fail path itself has never been deliberately exercised here.
3. [VERIFIED] **Nothing currently enforces libusb.** `build.sh` doesn't pass
   the flag; the flatpak manifest's `config-opts` don't either. Both current
   builds link it *incidentally* (rig: `YES 1.0.27` because the rig has
   libusb-dev; MXE image: `YES 1.0.27` because the image builds `libusb1`) —
   measured 2026-07-27, HANDOFF-packaging §"Current state". The **shipped
   AppImage must contain `libusb-1.0.so.0`** (Instrumentation).
4. [VERIFIED] **Linux artifact mismatch (G6)**: `build-release.sh linux`
   produces a flatpak; the shipped release artifact is an AppImage built
   outside the harness. Fix (replace/supplement with the linuxdeploy recipe
   + a libusb post-build assertion) is **handed off to the packaging
   agent** — documented, not applied (`docs/perf/tooling-gaps.md` G6).
5. [VERIFIED] **Compiler skew is a release gate**: the cross image's GCC 16
   makes implicit function declarations **errors** (default since GCC 14);
   the rig's GCC 13 only warns. A missing prototype merged clean on the rig
   and broke the Windows build (the `renderer.h` prototype incident). Run
   the cross build — or `-Werror=implicit-function-declaration` locally —
   before calling anything done (`runbooks/rig-perf-measurement.md` trap 9).
6. [VERIFIED] **Microsoft firmware (MCPX boot ROM, flash BIOS) is
   user-supplied and is NEVER committed** — the repo and all docs carry
   **hashes only**, for setup-identity verification
   (`runbooks/rig-perf-measurement.md` §Firmware;
   `docs/discord-perf-writeup.md` §Firmware). This repo is public-adjacent
   (allowlist-merged to `penguinvrlab/penguinbox`): a committed ROM is a
   license violation that cannot be fully retracted.
7. [DERIVED] **win64-cross output is static** (`--static`,
   `x86_64-w64-mingw32.static`): libusb ends up linked into the exe, so the
   Windows artifact has no DLL-presence question — only the configure-time
   one (invariant 1 still applies).
8. [DERIVED] The flatpak configure line differs from `build.sh` (no
   `--enable-renderdoc`; flags come from the manifest's `config-opts` +
   `cflags: -O3 -DXBOX=1`) — the flatpak binary is not feature-identical to
   a `build.sh` binary.

## Policies & decision points

- **Backends differ by target, on purpose** [VERIFIED, script header]:
  Windows = container cross-build (MXE); Linux = flatpak-builder in the SDK
  sandbox (which supplies SDL3's X11/Wayland deps). The Linux choice is the
  root of the G6 mismatch.
- **dsp56300 blob pre-seeded on the host** [VERIFIED]: meson's `fs.exists()`
  skips the configure-time download the image cannot perform.
- **Strip by default** (208 MB → ~32 MB); `--no-strip` for debugging.
- **Trust the meson summary, not the exit code** [VERIFIED]: confirm
  `libusb : YES <ver>`. Beware trap 7: SDL3's CMake subproject prints its
  own `| `-prefixed "Could NOT find LibUSB" lines that are NOT xemu's
  result (cost two wrong-diagnosis cycles).
- **app-id stays `app.xemu.xemu` until the deliberate rebrand** [VERIFIED,
  manifest note]: keeps icon/desktop/metainfo ids consistent; collides with
  a Flathub-installed xemu meanwhile.

## Failure modes

- **Silent libusb drop** (the flagship failure): green build, missing
  passthrough, discovered by a user with real hardware. Mitigation =
  invariants 2-3 + the Instrumentation checks.
- **Shipping the wrong Linux artifact class**: the harness implies a flatpak
  nobody ships; AppImage provenance unreproducible (G6).
- **Rig-only build confidence** (trap 9): merges that only GCC 13 accepts.
- **Rootless podman chown abort** on the dsp56300 blob (uid 1001) without
  `TAR_OPTIONS=--no-same-owner`.
- **Misattributed configure-log lines** (trap 7) → wrong dependency
  diagnosis.
- **Stale-binary trap** on the rig: testing yesterday's binary "disproves"
  code it never contained.
- **Firmware committed by accident** — catastrophic and irreversible for a
  public repo; nothing in the harness references ROM files, keep it that way.

## Instrumentation (how to verify an artifact)

- **libusb, configure time**: the unprefixed meson summary line
  `libusb : YES 1.0.27` (not SDL3's `| `-prefixed lines).
- **libusb, shipped AppImage** (the release gate, HANDOFF-packaging §3):
  ```sh
  ./PenguinBox-x86_64.AppImage --appimage-extract
  ls squashfs-root/usr/lib | grep libusb        # libusb-1.0.so.0 present
  ldd squashfs-root/usr/bin/xemu | grep libusb  # and actually linked
  ```
  If absent, pass it to linuxdeploy explicitly (`-l …/libusb-1.0.so.0`).
- **Windows exe**: `file dist/PenguinBox-win64.exe` + `sha256sum` (both
  printed by the script); size ~32 MB stripped.
- **Flatpak**: `flatpak install --user dist/PenguinBox-x86_64.flatpak` and
  launch.
- **Firmware identity**: `sha1sum` against the hash table above.
- **Rig binary freshness**: `build/qemu-system-i386` mtime newer than the
  change under test.

## Verified-by

- `docs/perf/loc/HANDOFF-packaging.md` (2026-07-27): the libusb bug
  precisely, the measured `YES 1.0.27` on both builds, the AppImage checks,
  the GCC-16 incident, and the flatpak-vs-AppImage note.
- `docs/perf/tooling-gaps.md` G6 (artifact mismatch + libusb; handed off)
  and G5 (strict-compiler gate).
- `runbooks/rig-perf-measurement.md` traps 7 and 9; §Firmware (hashes,
  user-supplied rule). Full hashes: `docs/discord-perf-writeup.md`
  §Firmware.
- `build-release.sh windows` has produced working exes repeatedly
  (HANDOFF: "solid"); the four baked-in fixes are listed there.

## Confidence & unknowns

- Whether the freedesktop 25.08 SDK provides `libusb-1.0` dev at flatpak
  build time (i.e. whether the flatpak silently lacks passthrough today) —
  never checked, because the flatpak has never shipped. [ASSUMED unknown]
- The `--enable-libusb` loud-fail path (invariant 2) traced through
  meson-buildoptions but never exercised end-to-end in this repo. [DERIVED]
- The exact AppImage build environment used for shipped releases (host
  distro, linuxdeploy version) is undocumented — the biggest reproducibility
  hole until G6 lands. [ASSUMED]
- macOS (`Darwin`) and MSYS-native paths are inherited upstream code paths,
  untested by this fork. [ASSUMED working]
- Whether `--enable-renderdoc` (forced by `build.sh`) belongs in release
  artifacts, and the flatpak/build.sh flag divergence (invariant 8) —
  undecided policy, flagged for the packaging agent.
