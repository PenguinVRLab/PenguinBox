#!/usr/bin/env bash
#
# build-release.sh — one entry point to build a distributable PenguinBox for a
# chosen OS, reproducibly, with every hard-won fix baked in.
#
#   ./build-release.sh windows      # -> dist/PenguinBox-win64.exe   (self-contained .exe)
#   ./build-release.sh linux        # -> dist/PenguinBox-x86_64.flatpak
#   ./build-release.sh appimage     # -> dist/PenguinBox-x86_64.AppImage
#
# Options:
#   --rebuild-image   (windows) force-rebuild the cross toolchain image
#   --no-strip        (windows) keep debug symbols (208 MB instead of ~32 MB)
#   --bin=PATH        (appimage) package this prebuilt qemu-system-i386 instead of running ./build.sh
#                     (a NAS-BUILD artifact, built in the jammy image: glibc 2.35, runs on SteamOS)
#   --print-opts      print the release configure options (below), one per line, and exit
#   -h | --help
#
# Release build flags (every target): LTO + x86-64-v3 (-Dx86_version=3, which also
# compiles every TU with -ffp-contract=off) + -mtune=znver2 + -mno-needed (no ISA-level
# stamp, so the program's own CPU check, not the loader, reports an old CPU). The CPU floor is therefore
# x86-64-v3 (AVX2/FMA: Intel Haswell 2013, AMD Excavator 2015 / Zen or newer); on an
# older CPU the binary prints which features are missing and exits instead of crashing
# (ui/xemu-cpu-floor.c). A developer's plain ./build.sh is unchanged (x86-64 baseline,
# no LTO). --bin= refuses a binary that was not built with these options.
#
# Backends differ by target, on purpose:
#   windows -> podman/docker + the MXE mingw-w64 cross image (ubuntu-win64-cross/)
#              running xemu's own ./build.sh -p win64-cross.
#   appimage-> native ./build.sh on this host, then linuxdeploy+appimagetool
#              bundle it. Same source tree and same build.sh the .exe uses, so
#              the two binaries are the same code. Runs on any glibc >= the
#              build host's, no install, no sandbox.
#   linux   -> flatpak-builder + flatpak/com.penguinvrlabs.penguinbox.yml. flatpak-builder
#              compiles xemu inside the freedesktop SDK sandbox (which supplies
#              all deps, incl. the X11/Wayland libs SDL3 needs), so it does NOT
#              use the Docker image. If flatpak-builder is absent it is installed
#              as the org.flatpak.Builder flatpak (flatpak --user, no sudo).
#
set -euo pipefail
IFS=$'\n\t'

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$REPO"

# The release configure options, passed to build.sh (which hands them to configure).
# Flags must go through configure's --extra-cflags= or a -D option: configure drops a bare
# -m/-f argument silently. flatpak/com.penguinvrlabs.penguinbox.yml carries the same three in its
# config-opts; the linux target refuses to build if they differ.
# -mno-needed: meson's x86_version=3 passes -mneeded, which stamps the binary "x86 ISA needed: x86-64-v3"; glibc
# >= 2.33 then refuses it on an older CPU with its own one-line error before any program code runs. Without the
# stamp the program's own CPU check (ui/xemu-cpu-floor.c) runs and names what is missing. configure appends repeated
# --extra-cflags= in order, after meson's own flags on the compile line.
RELEASE_OPTS=(--enable-lto -Dx86_version=3 --extra-cflags=-mtune=znver2 --extra-cflags=-mno-needed)
RELEASE_OPTS_LINE="$(IFS=' '; echo "${RELEASE_OPTS[*]}")"   # for messages (IFS below is newline/tab)
# Only an x86-64-v3 build carries this text (ui/xemu-cpu-floor.c): how --bin= tells a
# release binary from a developer build.
V3_MARK='this CPU lacks AVX2/FMA'

TARGET=""
REBUILD_IMAGE=0
STRIP=1
PREBUILT=""
for a in "$@"; do
  case "$a" in
    windows|win|win64) TARGET=windows ;;
    linux)             TARGET=linux ;;
    appimage|AppImage) TARGET=appimage ;;
    --rebuild-image)   REBUILD_IMAGE=1 ;;
    --no-strip)        STRIP=0 ;;
    --bin=*)           PREBUILT="${a#--bin=}" ;;
    --print-opts)      printf '%s\n' "${RELEASE_OPTS[@]}"; exit 0 ;;
    -h|--help)         grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown arg: $a" >&2; exit 2 ;;
  esac
done
if [ -z "$TARGET" ]; then
  echo "usage: $0 <windows|linux|appimage> [--rebuild-image] [--no-strip]" >&2
  exit 2
fi

log() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------------------
build_windows() {
  local IMAGE=xemu-win64-cross:latest
  local DOCKERFILE=ubuntu-win64-cross/gcc.Dockerfile
  local CONTEXT=ubuntu-win64-cross

  # Container engine (podman preferred; docker fallback).
  local ENGINE
  ENGINE="$(command -v podman || command -v docker || true)"
  [ -n "$ENGINE" ] || { echo "need podman or docker for the windows build" >&2; exit 1; }

  image_exists() {
    if [ "$(basename "$ENGINE")" = podman ]; then "$ENGINE" image exists "$IMAGE"
    else "$ENGINE" image inspect "$IMAGE" >/dev/null 2>&1; fi
  }

  if [ "$REBUILD_IMAGE" = 1 ] || ! image_exists; then
    log "building cross toolchain image $IMAGE (one-time, ~tens of minutes)"
    "$ENGINE" build -t "$IMAGE" -f "$DOCKERFILE" "$CONTEXT"
  else
    log "cross toolchain image $IMAGE present (use --rebuild-image to refresh)"
  fi

  # dsp56300 (Xbox APU DSP) fetches a prebuilt blob at configure time via the
  # `curl` PROGRAM, which the image lacks. Pre-seed it on the host so meson's
  # fs.exists() check skips the download entirely.
  local DSP_VER=0.1.3 TRIP=x86_64-pc-windows-gnu
  local BLOB="subprojects/dsp56300/dsp56300-${DSP_VER}-${TRIP}.tar.gz"
  if [ ! -f "$BLOB" ]; then
    log "pre-seeding dsp56300 blob ($TRIP)"
    curl -fsSL "https://github.com/mborgerson/dsp56300/releases/download/v${DSP_VER}/dsp56300-${DSP_VER}-${TRIP}.tar.gz" -o "$BLOB"
  fi

  rm -rf build dist
  # TAR_OPTIONS=--no-same-owner: the dsp56300 blob stores uid 1001; under
  # rootless podman tar cannot chown to it and aborts. This suppresses the chown.
  log "cross-compiling (./build.sh -p win64-cross)"
  "$ENGINE" run --rm --security-opt label=disable \
    -e TAR_OPTIONS=--no-same-owner \
    -v "$REPO":/xemu -w /xemu "$IMAGE" \
    ./build.sh -p win64-cross "${RELEASE_OPTS[@]}"

  [ -f dist/xemu.exe ] || { echo "build produced no dist/xemu.exe" >&2; exit 1; }

  if [ "$STRIP" = 1 ]; then
    log "stripping -> dist/PenguinBox-win64.exe"
    "$ENGINE" run --rm --security-opt label=disable -v "$REPO":/xemu -w /xemu "$IMAGE" \
      x86_64-w64-mingw32.static-strip -o dist/PenguinBox-win64.exe dist/xemu.exe
  else
    cp dist/xemu.exe dist/PenguinBox-win64.exe
  fi

  log "done"
  file dist/PenguinBox-win64.exe
  ( command -v sha256sum >/dev/null && sha256sum dist/PenguinBox-win64.exe ) || true
}

# ---------------------------------------------------------------------------
build_linux() {
  local MANIFEST=flatpak/com.penguinvrlabs.penguinbox.yml
  local APPID=com.penguinvrlabs.penguinbox
  local STATE=.flatpak-builder
  local FLATREPO="$STATE/repo"
  local OUT=dist/PenguinBox-x86_64.flatpak
  local FLATHUB=https://flathub.org/repo/flathub.flatpakrepo

  command -v flatpak >/dev/null || { echo "need flatpak on the host for the linux build" >&2; exit 1; }
  local o
  for o in "${RELEASE_OPTS[@]}"; do
    grep -qxF -- "      - $o" "$MANIFEST" || {
      echo "$MANIFEST config-opts lack the release option '$o' (release options: $RELEASE_OPTS_LINE)" >&2; exit 1; }
  done

  # Pick a flatpak-builder: host binary, else the org.flatpak.Builder flatpak,
  # else install that flatpak (--user, no sudo).
  local FB
  if command -v flatpak-builder >/dev/null; then
    FB=(flatpak-builder)
  elif flatpak info org.flatpak.Builder >/dev/null 2>&1; then
    FB=(flatpak run org.flatpak.Builder)
  else
    log "installing flatpak-builder (org.flatpak.Builder, flatpak --user, no sudo)"
    flatpak --user remote-add --if-not-exists flathub "$FLATHUB"
    flatpak --user install -y flathub org.flatpak.Builder
    FB=(flatpak run org.flatpak.Builder)
  fi

  mkdir -p dist
  log "flatpak-builder (pulls freedesktop runtime/SDK 25.08 on first run, ~1.5-2 GB)"
  "${FB[@]}" --user --force-clean --install-deps-from=flathub \
    --repo="$FLATREPO" "$STATE/build" "$MANIFEST"

  log "exporting single-file bundle -> $OUT"
  flatpak build-bundle --runtime-repo="$FLATHUB" "$FLATREPO" "$OUT" "$APPID"

  # The runtime must not need libfuse2 (see the appimagetool note above): assert it, not assume it.
  if head -c 2000000 "$OUT" | strings | grep -q 'libfuse\.so\.2'; then
    echo "FATAL: $OUT carries the old runtime that needs libfuse.so.2 -- it will not start on Ubuntu 22.04+ or CachyOS without fuse2." >&2
    exit 1
  fi
  log "runtime: no libfuse.so.2 dependency"

  log "done"
  ls -la "$OUT"
  ( command -v sha256sum >/dev/null && sha256sum "$OUT" ) || true
  echo "install with:  flatpak install --user $OUT"
}

# ---------------------------------------------------------------------------
# AppImage: native build of THIS tree, bundled with its shared libraries.
#
# Deliberately the same ./build.sh the win64-cross target runs -- there is no
# VR build switch, so the AppImage and the .exe are the same code. VR stays
# inert here for the same two reasons it is inert on Windows:
#   * config_spec.yml ships vr.enable = false
#   * the OpenXR runtime is dlopen'd (vr_xr_loader.c), and we do NOT bundle
#     libopenxr_loader.so -- so the loader soft-fails to NULL even if a user
#     flips the config. Nothing is stripped from the source to achieve this.
build_appimage() {
  local BIN=build/qemu-system-i386
  local APPDIR=build/AppDir
  local OUT=dist/PenguinBox-x86_64.AppImage
  local TOOLS="${XDG_CACHE_HOME:-$HOME/.cache}/penguinbox-appimage"

  # AppImage tooling is itself distributed as AppImages; extract-and-run avoids
  # needing FUSE, which headless/CI hosts usually lack.
  export APPIMAGE_EXTRACT_AND_RUN=1

  # build.sh now passes --enable-libusb unconditionally (see the note there),
  # so a missing dependency fails configure instead of silently dropping USB
  # passthrough. That is exactly how the
  # first AppImage shipped without usb-host while the Windows .exe had it --
  # the Steel Battalion community plays with real OEM controllers over
  # passthrough, so a quietly-missing libusb is a broken build, not a lesser one.
  # The flag turns a silent omission into a hard configure failure.
  if [ -n "$PREBUILT" ]; then
    [ -x "$PREBUILT" ] || { echo "--bin: $PREBUILT is not an executable" >&2; exit 1; }
    grep -qaF "$V3_MARK" "$PREBUILT" || {
      echo "--bin: $PREBUILT is not a release build (no x86-64-v3 CPU check in it)." >&2
      echo "       build it with: $RELEASE_OPTS_LINE" >&2
      exit 1; }
    log "packaging the prebuilt $PREBUILT ($(sha256sum "$PREBUILT" | cut -c1-16)...), no ./build.sh"
    BIN="$PREBUILT"
  else
    log "native build (./build.sh) -- same script the win64-cross target uses"
    ./build.sh "${RELEASE_OPTS[@]}"
    [ -x "$BIN" ] || { echo "build produced no $BIN" >&2; exit 1; }
  fi

  # Belt and braces: the flag should have failed the configure, but verify the
  # artifact rather than trusting it.
  if ! ldd "$BIN" | grep -qi 'libusb'; then
    echo "FATAL: $BIN is not linked against libusb -- USB passthrough would be missing." >&2
    echo "  install the dev package and rebuild:  sudo apt install -y libusb-1.0-0-dev" >&2
    exit 1
  fi
  log "libusb linked: $(ldd "$BIN" | grep -i libusb | awk '{print $1}' | head -1)"

  log "staging AppDir"
  rm -rf "$APPDIR"
  install -Dm755 "$BIN"                   "$APPDIR/usr/bin/qemu-system-i386"
  install -Dm644 data/xemu_64x64.png      "$APPDIR/usr/share/icons/hicolor/64x64/apps/penguinbox.png"
  install -Dm644 data/xemu_64x64.png      "$APPDIR/penguinbox.png"

  mkdir -p "$APPDIR/usr/share/applications" "$APPDIR/usr/lib"
  cat > "$APPDIR/usr/share/applications/penguinbox.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=PenguinBox
GenericName=Original Xbox Emulator
Comment=Original Xbox emulation (experimental)
Exec=qemu-system-i386
Icon=penguinbox
Terminal=false
Categories=Game;Emulator;
PrefersNonDefaultGPU=true
DESKTOP
  cp "$APPDIR/usr/share/applications/penguinbox.desktop" "$APPDIR/penguinbox.desktop"

  install -Dm755 scripts/penguinbox-sbc-rules.sh "$APPDIR/usr/bin/penguinbox-sbc-rules"
  install -Dm644 scripts/70-penguinvrlabs-sim-devices.rules "$APPDIR/usr/share/penguinbox/70-penguinvrlabs-sim-devices.rules"
  # The Sim Configurator's shipped documents (targets, devices), read on a Sim Configurator boot from <exe>/../share/penguinbox/sim-profiles
  install -Dm644 -t "$APPDIR/usr/share/penguinbox/sim-profiles" data/sim-profiles/*.yaml data/sim-profiles/SOURCE.md
  cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
# --install-sbc-rules: the one-time device rules for a real Steel Battalion controller (the family file, Valve's form)
if [ "${1:-}" = "--install-sbc-rules" ]; then
    exec "$HERE/usr/bin/penguinbox-sbc-rules"
fi
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/qemu-system-i386" "$@"
APPRUN
  chmod +x "$APPDIR/AppRun"

  mkdir -p "$TOOLS" dist
  local LD="$TOOLS/linuxdeploy-x86_64.AppImage"
  # The current appimagetool (AppImage/appimagetool, not the retired AppImageKit one): its type2 runtime is
  # static and needs no libfuse.so.2 on the user's machine. The old runtime dlopen'ed libfuse.so.2, which Ubuntu
  # 22.04+ and Arch/CachyOS no longer install by default, so the AppImage did not start there (2026-10-31).
  local AT="$TOOLS/appimagetool-static-x86_64.AppImage"
  [ -x "$LD" ] || { log "fetching linuxdeploy"; curl -fsSL -o "$LD" \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage; chmod +x "$LD"; }
  [ -x "$AT" ] || { log "fetching appimagetool"; curl -fsSL -o "$AT" \
      https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage; chmod +x "$AT"; }

  # libusb is on the AppImage excludelist, so linuxdeploy will NOT bundle it --
  # but we link it hard (not dlopen), so a host without libusb-1.0.so.0 could not
  # start the AppImage at all. Pass it explicitly. Path is discovered rather than
  # hardcoded so this survives a distro/arch change on the build host.
  local LIBUSB
  LIBUSB=$(ldd "$BIN" | awk '/libusb-1\.0\.so/ {print $3; exit}')
  [ -n "$LIBUSB" ] && [ -e "$LIBUSB" ] || {
    echo "FATAL: cannot locate libusb-1.0.so.0 to bundle (ldd gave: '$LIBUSB')" >&2; exit 1; }
  log "bundling shared libraries (linuxdeploy), incl. $LIBUSB"

  "$LD" --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/qemu-system-i386" \
        --library "$LIBUSB" \
        --desktop-file "$APPDIR/usr/share/applications/penguinbox.desktop" \
        --icon-file "$APPDIR/penguinbox.png"

  # Verify the bundle rather than trusting the flag: an unbundled hard-linked
  # libusb makes the AppImage refuse to start on any host without it.
  ls "$APPDIR"/usr/lib | grep -qi '^libusb-1\.0\.so' || {
    echo "FATAL: libusb was not bundled into the AppDir." >&2; exit 1; }
  log "libusb bundled: $(ls "$APPDIR"/usr/lib | grep -i '^libusb')"

  # Never ship an OpenXR loader: VR must stay dormant in this build.
  find "$APPDIR" -name 'libopenxr_loader*' -print -delete

  log "packaging -> $OUT"
  "$AT" "$APPDIR" "$OUT"

  log "done"
  ls -la "$OUT"
  ( command -v sha256sum >/dev/null && sha256sum "$OUT" ) || true
  echo "run with:  chmod +x $OUT && ./$OUT"
}

case "$TARGET" in
  windows) build_windows ;;
  linux)   build_linux ;;
  appimage) build_appimage ;;
esac
