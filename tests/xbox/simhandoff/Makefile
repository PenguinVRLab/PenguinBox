CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11 -DXEMU_YAML_LITE_STANDALONE -DXEMU_SIM_HANDOFF_STANDALONE
UI=../../../ui

# make check   the hand-off (ui/xemu-sim-handoff.c) as it ships, run against the stand-in configurator
#              (standin.py; needs python3 with PyYAML, as the app itself does)
# make drill   the RED drills: the result's request-id check removed; the store's folders not made; each must FAIL

all: simhandoff-test

simhandoff-test: simhandoff-test.c $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-handoff.h $(UI)/xemu-yaml-lite.c
	$(CC) -o $@ $(CFLAGS) -I$(UI) $< $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-dbus-stub.c $(UI)/xemu-yaml-lite.c

drill-noid: simhandoff-test.c $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-dbus-stub.c $(UI)/xemu-yaml-lite.c
	$(CC) -o $@ $(CFLAGS) -DSIM_HANDOFF_DRILL_NO_REQUEST_ID -I$(UI) $< $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-dbus-stub.c $(UI)/xemu-yaml-lite.c

drill-nomkdir: simhandoff-test.c $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-dbus-stub.c $(UI)/xemu-yaml-lite.c
	$(CC) -o $@ $(CFLAGS) -DSIM_HANDOFF_DRILL_NO_MKDIR -I$(UI) $< $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-dbus-stub.c $(UI)/xemu-yaml-lite.c

.PHONY: all check drill
check: all
	./simhandoff-test $(CURDIR)/standin.py

drill: drill-noid drill-nomkdir
	@if ./drill-nomkdir $(CURDIR)/standin.py > drill-nomkdir.log 2>&1; then echo "DRILL nomkdir: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL nomkdir: RED as expected ($$(grep -c '^FAIL' drill-nomkdir.log) FAIL lines: $$(grep '^FAIL' drill-nomkdir.log | head -2 | tr '\n' ' '))"; fi
	@if ./drill-noid $(CURDIR)/standin.py > drill-noid.log 2>&1; then echo "DRILL noid: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL noid: RED as expected ($$(grep -c '^FAIL' drill-noid.log) FAIL lines: $$(grep '^FAIL' drill-noid.log | head -2 | tr '\n' ' '))"; fi
