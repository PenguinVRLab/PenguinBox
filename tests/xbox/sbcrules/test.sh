#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test of scripts/penguinbox-sbc-rules.sh (--install-sbc-rules), run as this user into a scratch rules folder
# (its PENGUINBOX_RULES_* test variables; udevadm replaced by a recorder). Checks: the family file goes in
# verbatim with mode 0644 and the old 0666 file is removed; a second run says it is installed and changes nothing;
# a changed copy is reinstalled (the negative control of "already installed"); the shipped file holds no 0666 and
# every rule line follows a COPIED FROM VALVE note; the AppImage layout (share/penguinbox beside bin) is found.
#   sh tests/xbox/sbcrules/test.sh        exit 0 = every check passed
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../.." && pwd)
SCRIPT=$TOP/scripts/penguinbox-sbc-rules.sh
RULES=$TOP/scripts/70-penguinvrlabs-sim-devices.rules
T=$(mktemp -d /tmp/sbcrules-test-XXXXXX)
fails=0
checks=0
ok() { checks=$((checks + 1)); if [ "$2" = 0 ]; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

printf '%s\n' '#!/bin/sh' "echo \"\$*\" >> '$T/udevadm.log'" > "$T/udevadm"
chmod +x "$T/udevadm"
mkdir -p "$T/rules"
printf '%s\n' '# PenguinBox: a real Steel Battalion controller, for USB passthrough' \
    'SUBSYSTEM=="usb", ATTRS{idVendor}=="0a7b", ATTRS{idProduct}=="d000", TAG+="uaccess", MODE="0666"' \
    > "$T/rules/71-penguinbox-steel-battalion.rules"
run() { PENGUINBOX_RULES_DIR=$T/rules PENGUINBOX_UDEVADM=$T/udevadm PENGUINBOX_RULES_NO_PRIV=1 sh "$SCRIPT" "$@"; }

out=$(run 2>&1); rc=$?
ok "a first run succeeds" "$rc"
cmp -s "$RULES" "$T/rules/70-penguinvrlabs-sim-devices.rules"; ok "the family file is installed verbatim" $?
[ "$(stat -c %a "$T/rules/70-penguinvrlabs-sim-devices.rules" 2>/dev/null)" = 644 ]; ok "its mode is 0644" $?
[ ! -e "$T/rules/71-penguinbox-steel-battalion.rules" ]; ok "the old 0666 rule file is removed" $?
echo "$out" | grep -q "removing the old"; ok "the run says it removes the old file" $?
grep -q "control --reload-rules" "$T/udevadm.log" 2>/dev/null && grep -q "attr-match=idVendor=0a7b" "$T/udevadm.log"
ok "udev is reloaded and the controller re-triggered" $?

: > "$T/udevadm.log"
out=$(run 2>&1); rc=$?
[ "$rc" = 0 ] && echo "$out" | grep -q "already installed"; ok "a second run says it is already installed" $?
[ ! -s "$T/udevadm.log" ]; ok "... and runs nothing" $?

echo '# changed' >> "$T/rules/70-penguinvrlabs-sim-devices.rules"
run > /dev/null 2>&1
cmp -s "$RULES" "$T/rules/70-penguinvrlabs-sim-devices.rules"; ok "a changed copy is reinstalled" $?

! grep -q '0666' "$RULES"; ok "the shipped file holds no 0666" $?
bad=$(awk '/^[A-Z]/ { if (prev !~ /COPIED FROM VALVE/) n++ } { prev = $0 } END { print n + 0 }' "$RULES")
[ "$bad" = 0 ]; ok "every rule line follows a COPIED FROM VALVE note" $?
grep -v '^#' "$RULES" | grep -v '^$' | grep -qv 'MODE="0660", TAG+="uaccess"$'
[ $? != 0 ]; ok "every rule line ends in Valve's MODE=\"0660\", TAG+=\"uaccess\"" $?

mkdir -p "$T/app/usr/bin" "$T/app/usr/share/penguinbox"
cp "$SCRIPT" "$T/app/usr/bin/penguinbox-sbc-rules"
cp "$RULES" "$T/app/usr/share/penguinbox/"
rm -rf "$T/rules2"; mkdir -p "$T/rules2"
PENGUINBOX_RULES_DIR=$T/rules2 PENGUINBOX_UDEVADM=$T/udevadm PENGUINBOX_RULES_NO_PRIV=1 sh "$T/app/usr/bin/penguinbox-sbc-rules" > /dev/null 2>&1
cmp -s "$RULES" "$T/rules2/70-penguinvrlabs-sim-devices.rules"; ok "the AppImage layout finds its file (usr/share/penguinbox)" $?
rm "$T/app/usr/share/penguinbox/70-penguinvrlabs-sim-devices.rules"
PENGUINBOX_RULES_DIR=$T/rules2 sh "$T/app/usr/bin/penguinbox-sbc-rules" > /dev/null 2>&1
[ $? = 1 ]; ok "a build missing its rules file says so and fails" $?

rm -rf "$T"
echo "$checks checks, $fails failed"
[ "$fails" = 0 ]
