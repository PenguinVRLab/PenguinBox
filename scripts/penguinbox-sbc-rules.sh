#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# penguinbox-sbc-rules.sh -- install the Sim Configurator's shared device rules, so the person logged in at this computer
# may open a real Steel Battalion controller (0a7b:d000).
# Run by the AppImage as:  PenguinBox-x86_64.AppImage --install-sbc-rules
#
# The file is 70-penguinvrlabs-sim-devices.rules, owned by the Sim Configurator (its decisions S-47, S-48, S-49) and
# vendored UNCHANGED beside this script; it is installed verbatim. Every line in it is COPIED FROM VALVE's
# steam-devices rules (their form, MODE="0660", TAG+="uaccess"; no 0666). It replaces this script's old
# 71-penguinbox-steel-battalion.rules (MODE="0666"), which is removed (the owner's ruling of 2026-11-14, relayed by
# the Sim Configurator session).
#
# uaccess gives a device to the person logged in at this computer, while they are logged in, and to nobody else: an
# ssh session with nobody logged in at the seat gets nothing. Installing is the user's own act, with their own
# password, after this message says what is installed (the Sim Configurator's S-44).
#
# Root cannot read an AppImage's FUSE mount, so the rules travel on the privileged command's standard input, never as
# a path into the mount. SteamOS (Deck, Steam Machine): /etc keeps the file across updates; pkexec needs the user's
# password, so set one first in Desktop Mode with `passwd` if you never have.
#
# For a test only: PENGUINBOX_RULES_SRC (the file), PENGUINBOX_RULES_DIR (where it goes), PENGUINBOX_UDEVADM (the
# udevadm to run), PENGUINBOX_RULES_NO_PRIV=1 (run the install step as this user).
set -eu
NAME=70-penguinvrlabs-sim-devices.rules
HERE=$(dirname "$(readlink -f "$0")")
SRC=${PENGUINBOX_RULES_SRC:-}
if [ -z "$SRC" ]; then
    for c in "$HERE/../share/penguinbox/$NAME" "$HERE/$NAME"; do
        if [ -f "$c" ]; then SRC=$c; break; fi
    done
fi
if [ -z "$SRC" ] || [ ! -f "$SRC" ]; then
    echo "PenguinBox: this build is missing its device rules ($NAME, looked beside $HERE)." >&2
    exit 1
fi
DIR=${PENGUINBOX_RULES_DIR:-/etc/udev/rules.d}
NEW=$DIR/$NAME
OLD=$DIR/71-penguinbox-steel-battalion.rules
UDEVADM=${PENGUINBOX_UDEVADM:-udevadm}

if [ -f "$NEW" ] && cmp -s "$SRC" "$NEW" && [ ! -e "$OLD" ]; then
    echo "PenguinBox: the device rules are already installed ($NEW)."
    exit 0
fi
STEP="cat > '$NEW.tmp' && chmod 0644 '$NEW.tmp' && mv -f '$NEW.tmp' '$NEW' && rm -f '$OLD' && \
$UDEVADM control --reload-rules && $UDEVADM trigger --subsystem-match=usb --attr-match=idVendor=0a7b && \
$UDEVADM trigger --subsystem-match=hidraw"
echo "PenguinBox: installing $NEW, the Sim Configurator's shared device rules (every line copied from Valve's steam-devices"
echo "  form: the person logged in at this computer may open the device; nobody else)."
if [ -e "$OLD" ]; then
    echo "PenguinBox: and removing the old $OLD (it let every user open the controller)."
fi
if [ "$(id -u)" = 0 ] || [ -n "${PENGUINBOX_RULES_NO_PRIV:-}" ]; then
    sh -c "$STEP" < "$SRC"
elif command -v pkexec >/dev/null 2>&1; then
    pkexec sh -c "$STEP" < "$SRC"
elif command -v sudo >/dev/null 2>&1; then
    sudo sh -c "$STEP" < "$SRC"
else
    echo "PenguinBox: no pkexec or sudo. As root, run these, then unplug the controller and plug it back in:" >&2
    echo "  install -m 0644 '$SRC' '$NEW'   (copy the file out of the AppImage first: --appimage-extract)" >&2
    echo "  rm -f '$OLD'" >&2
    echo "  udevadm control --reload-rules" >&2
    exit 1
fi
echo "PenguinBox: done. Unplug the controller and plug it back in, then start PenguinBox."
