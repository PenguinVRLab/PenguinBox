CC=gcc
CXX=g++
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
CXXFLAGS=-O2 -Wall -Wextra -Werror -g -std=c++17
UI=../../../ui
RYML=../../../thirdparty/rapidyaml
DATA=../../../data/sim-profiles
# B: where the objects, the binaries, the logs and the made-up USB trees go (default: a folder here)
B ?= $(CURDIR)/out
HDRS=$(UI)/xemu-sim-sbc-source.h $(UI)/xemu-sbc-device.h $(UI)/xemu-sim-merge.h $(UI)/xemu-sim-target.h \
     $(UI)/xemu-sim-profile-reader.h

# make check          the real Steel Battalion controller as a Sim Configurator source (ui/xemu-sim-sbc-source.c):
#                     the 49 ids against the document, frames to inputs, where the controller is and the boot
#                     plan's predicate on made-up USB folders (absent, another position, two, no permission, the
#                     passthrough switch), and end to end through the setup's bindings,
#                     the merge layer and the writer to the emulated report; then the same under ASan and UBSan
# make drill          the RED drills (each must FAIL the test): the gear lever's 0 read as no reading
#                     (SIM_SBC_SOURCE_DRILL=1), the left and right pedals swapped (SIM_SBC_SOURCE_DRILL=2)
# make check-toggles  part T: the five latching toggles end to end as LEVELS (run by `make check` too): a toggle
#                     bound from the real controller's own switch follows the switch's position
#                     (sim_target_write_sbc_levels with sim_sbc_toggle_level_mask); it failed 5 of 7 while the
#                     writer flipped every toggle on a press edge

all: $(B)/simsbc-test

$(B)/.dir:
	mkdir -p $(B) && touch $@

$(B)/ryml.o: $(RYML)/ryml.cc $(RYML)/rapidyaml-0.12.1.hpp | $(B)/.dir
	$(CXX) -o $@ -c -O2 -std=c++17 -I$(RYML) $<

$(B)/reader.o: $(UI)/xemu-sim-profile-reader.cc $(UI)/xemu-sim-profile-reader.h | $(B)/.dir
	$(CXX) -o $@ -c $(CXXFLAGS) -I$(UI) -I$(RYML) $<

$(B)/merge.o: $(UI)/xemu-sim-merge.c $(HDRS) | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/target.o: $(UI)/xemu-sim-target.c $(HDRS) | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/source.o: $(UI)/xemu-sim-sbc-source.c $(HDRS) | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/source-drill%.o: $(UI)/xemu-sim-sbc-source.c $(HDRS) | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -DSIM_SBC_SOURCE_DRILL=$* -I$(UI) $<

$(B)/bootplan.o: $(UI)/xemu-sim-bootplan.c $(UI)/xemu-sim-bootplan.h $(HDRS) | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

$(B)/evdev.o: $(UI)/xemu-sim-evdev.c $(UI)/xemu-sim-evdev.h $(UI)/xemu-sim-profile-reader.h | $(B)/.dir
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

OBJS=$(B)/merge.o $(B)/target.o $(B)/bootplan.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o

$(B)/simsbc-test: simsbc-test.cc $(HDRS) $(B)/source.o $(OBJS)
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) $< $(B)/source.o $(OBJS) -lm

$(B)/drill-%: simsbc-test.cc $(HDRS) $(B)/source-drill%.o $(OBJS)
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) $< $(B)/source-drill$*.o $(OBJS) -lm

SAN=-O1 -fsanitize=address,undefined -fno-sanitize-recover=all
$(B)/simsbc-test-san: simsbc-test.cc $(HDRS) $(UI)/xemu-sim-sbc-source.c $(UI)/xemu-sim-merge.c \
                      $(UI)/xemu-sim-target.c $(B)/bootplan.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o
	$(CC) -o $(B)/source-san.o -c $(CFLAGS) $(SAN) -I$(UI) $(UI)/xemu-sim-sbc-source.c
	$(CC) -o $(B)/merge-san.o -c $(CFLAGS) $(SAN) -I$(UI) $(UI)/xemu-sim-merge.c
	$(CC) -o $(B)/target-san.o -c $(CFLAGS) $(SAN) -I$(UI) $(UI)/xemu-sim-target.c
	$(CC) -o $(B)/bootplan-san.o -c $(CFLAGS) $(SAN) -I$(UI) $(UI)/xemu-sim-bootplan.c
	$(CXX) -o $@ $(CXXFLAGS) $(SAN) -I$(UI) $< $(B)/source-san.o $(B)/merge-san.o $(B)/target-san.o \
	    $(B)/bootplan-san.o $(B)/evdev.o $(B)/reader.o $(B)/ryml.o -lm

.PHONY: all check drill check-toggles
check: $(B)/simsbc-test $(B)/simsbc-test-san
	$(B)/simsbc-test $(DATA) . $(B)/work
	$(B)/simsbc-test $(DATA) . $(B)/work toggles
	@echo "== the same under ASan and UBSan"
	@$(B)/simsbc-test-san $(DATA) . $(B)/work-san > $(B)/san.log 2>&1 || { grep -m3 "runtime error\|Sanitizer\|^FAIL" $(B)/san.log; echo "FAIL the sanitizer run"; exit 1; }
	@echo "ok   the sanitizer run: $$(tail -1 $(B)/san.log), no report"

drill: $(B)/drill-1 $(B)/drill-2
	@for n in 1 2; do \
	   if $(B)/drill-$$n $(DATA) . $(B)/work-drill$$n > $(B)/drill-$$n.log 2>&1; then \
	     echo "DRILL $$n: the test PASSED (no power)"; exit 1; \
	   else echo "DRILL $$n: RED as expected ($$(grep -c '^FAIL' $(B)/drill-$$n.log) FAIL lines: $$(grep '^FAIL' $(B)/drill-$$n.log | head -2 | cut -c1-110 | tr '\n' '|'))"; fi; \
	 done

check-toggles: $(B)/simsbc-test
	$(B)/simsbc-test $(DATA) . $(B)/work toggles
