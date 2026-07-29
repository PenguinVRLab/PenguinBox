# Handoff — packaging notes (USB passthrough / libusb)

For whoever owns the AppImage + Windows packaging. Written 2026-07-27 by the
agent that did the Line of Contact renderer work, after community feedback that
**USB passthrough was missing from the packaged Linux build**.

## The bug, precisely

`meson_options.txt:199` declares:

```
option('libusb', type : 'feature', value : 'auto',
       description: 'libusb support for USB passthrough')
```

**`auto` means silent omission.** If `libusb-1.0` dev headers aren't present at
configure time, meson drops USB passthrough and the build *still succeeds*.
Nothing in the output shouts about it. `build.sh` never passes `--enable-libusb`,
so every build we produce is at the mercy of whatever happens to be installed on
the build machine.

That is exactly the reported symptom: a user building from source (with
`libusb-dev` installed) got working passthrough; the packaged build did not.

**This matters more here than in most forks** — USB passthrough is how someone
uses a *real Steel Battalion controller*, which is a large part of this project's
reason to exist.

## The fix (recommended, not yet applied)

1. **Make it mandatory in `build.sh`** so a missing dependency fails the build
   loudly instead of shipping a crippled binary:
   ```sh
   opts="$opts --enable-libusb"
   ```
   Community suggestion, and correct: better a red build than a silent
   regression that only surfaces when a user plugs in hardware.

2. **Confirm it landed** — check the meson summary, not just the exit code:
   ```
   libusb                          : YES 1.0.27
   ```
   Beware: SDL3's CMake subproject *also* searches for libusb and prints its own
   "Could NOT find LibUSB" lines prefixed with `| `. Those are SDL3's HIDAPI
   search and are **not** xemu's result. Grep for the unprefixed
   `Run-time dependency libusb-1.0` / summary line.

3. **Bundle it in the AppImage.** `linuxdeploy` copies the binary's shared
   library dependencies into the AppDir, so once the binary genuinely links
   `libusb-1.0.so.0` it should be bundled automatically — but verify:
   ```sh
   ./PenguinBox-x86_64.AppImage --appimage-extract
   ls squashfs-root/usr/lib | grep libusb
   ldd squashfs-root/usr/bin/xemu | grep libusb
   ```
   If it's absent, pass it explicitly (`-l /usr/lib/.../libusb-1.0.so.0`).

## Current state as measured (2026-07-27)

| build | libusb | note |
|---|---|---|
| rig Linux (`./build.sh`) | **YES 1.0.27** | rig has `libusb-dev`; incidental, not guaranteed |
| Windows cross (MXE image) | **YES 1.0.27** | the cross image builds `libusb1`; also incidental |

Both happen to be fine *today*. Neither is enforced.

## Also worth knowing

- **`build-release.sh linux` produces a flatpak, not an AppImage.** The artifact
  that actually ships is `PenguinBox-x86_64.AppImage`, built outside that script.
  Either point the script at `linuxdeploy` (upstream's `build-linux.yml` recipe:
  fetch `linuxdeploy-x86_64.AppImage`, unpack the deb into an AppDir, install
  `xemu.metainfo.xml`, run `linuxdeploy --output appimage --appdir appimage`) or
  drop the flatpak path so the harness stops implying an artifact nobody ships.
- **The Windows path in `build-release.sh` is solid** and has produced working
  exes repeatedly. It bakes in four fixes worth keeping: `-p win64-cross` (the
  platform flag, easy to lose), a pre-seeded `dsp56300` blob (the image lacks the
  `curl` CLI), `TAR_OPTIONS=--no-same-owner` (rootless podman can't chown to the
  blob's uid), and a strip pass (208 MB → 32 MB).
- **Build with a modern compiler before releasing.** The rig's GCC 13 only warns
  on implicit function declarations; the cross toolchain's GCC 16 makes them
  errors. A missing prototype merged cleanly on the rig and broke the Windows
  build — see the `renderer.h` prototype fix committed alongside this note.

## Renderer flags the packages should know about

All five are **opt-in, default OFF**, each with an `XEMU_NO_<NAME>=1` kill
switch. Line of Contact wants all of them; Steel Battalion should be left on
defaults for now (they work there but its visuals have not been checked with
them on):

```
XEMU_SURF2TEX_EXT=1 XEMU_ALIAS_SURFACES=1 XEMU_SURF2TEX_ZEROCOPY=1 \
XEMU_SURF2TEX_FEEDBACK=1 XEMU_TLB_FASTPATH=1
```

**Caveat to carry into any release notes:** Vulkan validation layers were not
active during this work (`display.vulkan.validation_layers` was unset, and
enabling it produced no output — the plumbing needs its own fix). None of the
renderer work is validation-checked.
