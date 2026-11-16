#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Test of the D-Bus hand-off route (tests/xbox/simdbus): builds the test and the stand-in service with gio, starts a
# private session bus whose service folder holds com.penguinvrlabs.simconfigurator.service (Exec = the stand-in),
# runs the test against it, and stops the bus. Needs gio-2.0 headers and dbus-daemon (the NAS build image has both;
# the dev box has neither).
#   sh tests/xbox/simdbus/run.sh [WORK_DIR]          exit 0 = every check passed
#   RED drill: SIM_DBUS_DRILL=1 builds the route with the Busy error read as accepted; the test must fail
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
UI=$(cd "$HERE/../../../ui" && pwd)
W=${1:-$(mktemp -d /tmp/simdbus-XXXXXX)}
mkdir -p "$W/services" "$W/data"
DEFS="-DXEMU_SIM_DBUS_STANDALONE -DXEMU_SIM_HANDOFF_STANDALONE -DXEMU_YAML_LITE_STANDALONE"
[ -n "${SIM_DBUS_DRILL:-}" ] && DEFS="$DEFS -DSIM_DBUS_DRILL_BUSY_ACCEPTED"
# shellcheck disable=SC2046,SC2086
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror $DEFS -I"$UI" -o "$W/simdbus-test" "$HERE/simdbus-test.c" \
    "$UI/xemu-sim-handoff.c" "$UI/xemu-sim-dbus.c" "$UI/xemu-yaml-lite.c" $(pkg-config --cflags --libs gio-2.0) \
    || { echo "FAIL build of the test"; exit 1; }
# shellcheck disable=SC2046
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -o "$W/standin-service" "$HERE/standin-service.c" \
    $(pkg-config --cflags --libs gio-2.0) || { echo "FAIL build of the stand-in"; exit 1; }
printf '[D-BUS Service]\nName=com.penguinvrlabs.simconfigurator\nExec=%s %s\n' "$W/standin-service" "$W/mode" \
    > "$W/services/com.penguinvrlabs.simconfigurator.service"
cat > "$W/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:dir=$W</listen>
  <servicedir>$W/services</servicedir>
  <policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy>
</busconfig>
EOF
OUT=$(dbus-daemon --config-file="$W/bus.conf" --fork --print-address=1 --print-pid=1) || { echo "FAIL dbus-daemon"; exit 1; }
ADDR=$(echo "$OUT" | sed -n 1p)
PID=$(echo "$OUT" | sed -n 2p)
DBUS_SESSION_BUS_ADDRESS=$ADDR "$W/simdbus-test" "$W/mode" "$W/data"
rc=$?
kill "$PID" 2>/dev/null
# a second bus with no service file: the app is not installed
mkdir -p "$W/none"
sed -e "s#<servicedir>$W/services</servicedir>#<servicedir>$W/none</servicedir>#" "$W/bus.conf" > "$W/bus2.conf"
OUT=$(dbus-daemon --config-file="$W/bus2.conf" --fork --print-address=1 --print-pid=1) || { echo "FAIL dbus-daemon 2"; exit 1; }
DBUS_SESSION_BUS_ADDRESS=$(echo "$OUT" | sed -n 1p) SIMDBUS_NOT_INSTALLED=1 "$W/simdbus-test" "$W/mode" "$W/data" || rc=1
kill "$(echo "$OUT" | sed -n 2p)" 2>/dev/null
# a third bus whose service file names an Exec that cannot run: a broken install (Spawn.ExecFailed or the like)
mkdir -p "$W/broken"
printf '#!/nonexistent/interpreter\n' > "$W/broken-exec"
chmod 0755 "$W/broken-exec"
printf '[D-BUS Service]\nName=com.penguinvrlabs.simconfigurator\nExec=%s\n' "$W/broken-exec" \
    > "$W/broken/com.penguinvrlabs.simconfigurator.service"
sed -e "s#<servicedir>$W/services</servicedir>#<servicedir>$W/broken</servicedir>#" "$W/bus.conf" > "$W/bus3.conf"
OUT=$(dbus-daemon --config-file="$W/bus3.conf" --fork --print-address=1 --print-pid=1) || { echo "FAIL dbus-daemon 3"; exit 1; }
DBUS_SESSION_BUS_ADDRESS=$(echo "$OUT" | sed -n 1p) SIMDBUS_BROKEN_INSTALL=1 "$W/simdbus-test" "$W/mode" "$W/data" || rc=1
kill "$(echo "$OUT" | sed -n 2p)" 2>/dev/null
exit $rc
