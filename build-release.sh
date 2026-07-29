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
#   -h | --help
#
# Backends differ by target, on purpose:
#   windows -> podman/docker + the MXE mingw-w64 cross image (ubuntu-win64-cross/)
#              running xemu's own ./build.sh -p win64-cross.
#   appimage-> native ./build.sh on this host, then linuxdeploy+appimagetool
#              bundle it. Same source tree and same build.sh the .exe uses, so
#              the two binaries are the same code. Runs on any glibc >= the
#              build host's, no install, no sandbox.
#   linux   -> flatpak-builder + flatpak/app.xemu.xemu.yml. flatpak-builder
#              compiles xemu inside the freedesktop SDK sandbox (which supplies
#              all deps, incl. the X11/Wayland libs SDL3 needs), so it does NOT
#              use the Docker image. If flatpak-builder is absent it is installed
#              as the org.flatpak.Builder flatpak (flatpak --user, no sudo).
#
set -euo pipefail
IFS=$'\n\t'

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$REPO"

TARGET=""
REBUILD_IMAGE=0
STRIP=1
for a in "$@"; do
  case "$a" in
    windows|win|win64) TARGET=windows ;;
    linux)             TARGET=linux ;;
    appimage|AppImage) TARGET=appimage ;;
    --rebuild-image)   REBUILD_IMAGE=1 ;;
    --no-strip)        STRIP=0 ;;
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
    ./build.sh -p win64-cross

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
  local MANIFEST=flatpak/app.xemu.xemu.yml
  local APPID=app.xemu.xemu
  local STATE=.flatpak-builder
  local FLATREPO="$STATE/repo"
  local OUT=dist/PenguinBox-x86_64.flatpak
  local FLATHUB=https://flathub.org/repo/flathub.flatpakrepo

  command -v flatpak >/dev/null || { echo "need flatpak on the host for the linux build" >&2; exit 1; }

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
  log "native build (./build.sh) -- same script the win64-cross target uses"
  ./build.sh
  [ -x "$BIN" ] || { echo "build produced no $BIN" >&2; exit 1; }

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
DESKTOP
  cp "$APPDIR/usr/share/applications/penguinbox.desktop" "$APPDIR/penguinbox.desktop"

  cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/qemu-system-i386" "$@"
APPRUN
  chmod +x "$APPDIR/AppRun"

  mkdir -p "$TOOLS" dist
  local LD="$TOOLS/linuxdeploy-x86_64.AppImage"
  local AT="$TOOLS/appimagetool-x86_64.AppImage"
  [ -x "$LD" ] || { log "fetching linuxdeploy"; curl -fsSL -o "$LD" \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage; chmod +x "$LD"; }
  [ -x "$AT" ] || { log "fetching appimagetool"; curl -fsSL -o "$AT" \
      https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage; chmod +x "$AT"; }

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
