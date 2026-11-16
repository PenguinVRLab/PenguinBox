CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
UI=../../../ui
# B: where the objects, the binaries, the logs and the test's made-up tree go (default: a folder here)
B ?= $(CURDIR)/out

# make check   the evdev source (ui/xemu-sim-evdev.c) on a made-up tree: links, sysfs, udev records, FIFO nodes; then
#              the same test and source again under AddressSanitizer and UndefinedBehaviorSanitizer (any report fails)
# make drill   the RED drill: an ABS event taken from any node of the unit (SIM_EVDEV_DRILL_ANY_NODE); must FAIL
# make list    the units on THIS machine, their port paths and codes (opens nodes read-only, never grabs; not shipped)

all: $(B)/simevdev-test $(B)/list-units

$(B)/.dir:
	mkdir -p $(B) && touch $@

$(B)/evdev.o: $(UI)/xemu-sim-evdev.c $(UI)/xemu-sim-evdev.h $(UI)/xemu-sim-profile-reader.h | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/evdev-drill.o: $(UI)/xemu-sim-evdev.c $(UI)/xemu-sim-evdev.h $(UI)/xemu-sim-profile-reader.h | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -DSIM_EVDEV_DRILL_ANY_NODE -I$(UI) $<

$(B)/simevdev-test: simevdev-test.c $(B)/evdev.o
	$(CC) -o $@ $(CFLAGS) -I$(UI) $< $(B)/evdev.o

$(B)/simevdev-test-san: simevdev-test.c $(UI)/xemu-sim-evdev.c $(UI)/xemu-sim-evdev.h | $(B)/.dir
	$(CC) -o $@ $(CFLAGS) -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -I$(UI) $< $(UI)/xemu-sim-evdev.c

$(B)/drill-anynode: simevdev-test.c $(B)/evdev-drill.o
	$(CC) -o $@ $(CFLAGS) -I$(UI) $< $(B)/evdev-drill.o

$(B)/list-units: list-units.c $(B)/evdev.o
	$(CC) -o $@ $(CFLAGS) -I$(UI) $< $(B)/evdev.o

.PHONY: all check drill list
check: $(B)/simevdev-test $(B)/simevdev-test-san
	$(B)/simevdev-test $(B)/work
	@echo "== the same under ASan and UBSan"
	$(B)/simevdev-test-san $(B)/work-san > $(B)/san.log 2>&1 || { grep -m3 "runtime error\|Sanitizer\|^FAIL" $(B)/san.log; echo "FAIL the sanitizer run"; exit 1; }
	@echo "ok   the sanitizer run: $$(tail -1 $(B)/san.log), no report"

drill: $(B)/drill-anynode
	@if $(B)/drill-anynode $(B)/work-drill > $(B)/drill-anynode.log 2>&1; then \
	   echo "DRILL anynode: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL anynode: RED as expected ($$(grep -c '^FAIL' $(B)/drill-anynode.log) FAIL lines: $$(grep '^FAIL' $(B)/drill-anynode.log | head -2 | tr '\n' ' '))"; fi

list: $(B)/list-units
	$(B)/list-units
