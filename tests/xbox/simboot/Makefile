CC=gcc
CXX=g++
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
CXXFLAGS=-O2 -Wall -Wextra -Werror -g -std=c++17
UI=../../../ui
RYML=../../../thirdparty/rapidyaml
DATA=../../../data/sim-profiles
TESTS=..
# B: where the objects, the binaries, the logs and the test's scratch documents go (default: a folder here)
B ?= $(CURDIR)/out

# make check   the boot plan (ui/xemu-sim-bootplan.c) on hand-built ports, on real documents loaded by the reader, and
#              where the shipped documents are found
# make drill   the RED drill: a failing port let through (SIM_BOOTPLAN_DRILL_LET_THROUGH); the test must FAIL

all: $(B)/simboot-test

# make check-hotkey   the hotkeys on keys a setup binds (ui/xemu-sim-boot.h, header only), on hand-built ports
$(B)/simboot-hotkey-test: simboot-hotkey-test.cc $(UI)/xemu-sim-boot.h $(UI)/xemu-sim-keys.h | $(B)/.dir
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) $<

.PHONY: check-hotkey
check-hotkey: $(B)/simboot-hotkey-test
	$(B)/simboot-hotkey-test

# make check-preflight   the app's pre-flight verdict (ui/xemu-sim-boot.h) after the hand-off's own judge
$(B)/simboot-preflight-test: simboot-preflight-test.c $(UI)/xemu-sim-boot.h $(UI)/xemu-sim-handoff.c $(UI)/xemu-yaml-lite.c | $(B)/.dir
	$(CC) -o $@ $(CFLAGS) -DXEMU_YAML_LITE_STANDALONE -DXEMU_SIM_HANDOFF_STANDALONE -I$(UI) $< $(UI)/xemu-sim-handoff.c $(UI)/xemu-sim-dbus-stub.c $(UI)/xemu-yaml-lite.c

.PHONY: check-preflight
check-preflight: $(B)/simboot-preflight-test
	$(B)/simboot-preflight-test $(B)/work-preflight

$(B)/.dir:
	mkdir -p $(B) && touch $@

$(B)/ryml.o: $(RYML)/ryml.cc $(RYML)/rapidyaml-0.12.1.hpp | $(B)/.dir
	$(CXX) -o $@ -c -O2 -std=c++17 -I$(RYML) $<

$(B)/reader.o: $(UI)/xemu-sim-profile-reader.cc $(UI)/xemu-sim-profile-reader.h | $(B)/.dir
	$(CXX) -o $@ -c $(CXXFLAGS) -I$(UI) -I$(RYML) $<

$(B)/bootplan.o: $(UI)/xemu-sim-bootplan.c $(UI)/xemu-sim-bootplan.h $(UI)/xemu-sim-keys.h $(UI)/xemu-sim-evdev.h $(UI)/xemu-sim-port.h | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/bootplan-drill.o: $(UI)/xemu-sim-bootplan.c $(UI)/xemu-sim-bootplan.h $(UI)/xemu-sim-keys.h $(UI)/xemu-sim-evdev.h $(UI)/xemu-sim-port.h | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -DSIM_BOOTPLAN_DRILL_LET_THROUGH -I$(UI) $<

$(B)/evdev.o: $(UI)/xemu-sim-evdev.c $(UI)/xemu-sim-evdev.h $(UI)/xemu-sim-profile-reader.h | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/simboot-test: simboot-test.cc $(B)/bootplan.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) $< $(B)/bootplan.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o

$(B)/drill-letthrough: simboot-test.cc $(B)/bootplan-drill.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) $< $(B)/bootplan-drill.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o

.PHONY: all check drill
check: all check-hotkey check-preflight
	$(B)/simboot-test $(DATA) $(TESTS) $(B)/work

drill: $(B)/drill-letthrough
	@if $(B)/drill-letthrough $(DATA) $(TESTS) $(B)/work-drill > $(B)/drill-letthrough.log 2>&1; then \
	   echo "DRILL letthrough: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL letthrough: RED as expected ($$(grep -c '^FAIL' $(B)/drill-letthrough.log) FAIL lines: $$(grep '^FAIL' $(B)/drill-letthrough.log | head -2 | tr '\n' ' '))"; fi
