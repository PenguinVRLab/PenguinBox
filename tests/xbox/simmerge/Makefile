CC=gcc
CXX=g++
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
CXXFLAGS=-O2 -Wall -Wextra -Werror -g -std=c++17
UI=../../../ui
RYML=../../../thirdparty/rapidyaml
DATA=../../../data/sim-profiles
# every object includes these (the reader's structs are laid out in its header): a changed header rebuilds them all
HDRS=$(UI)/xemu-sim-merge.h $(UI)/xemu-sim-target.h $(UI)/xemu-sim-profile-reader.h

# make check   the merge layer's core against the app's own numbers, the hand vectors and the end-to-end frames;
#              then the same checks with the merge layer and the writers under UBSan (make ub-check)
# make drill   the RED drill: the aiming axis left truncated; the test must FAIL
# make keys    the evdev-to-SDL key table against <linux/input-event-codes.h> and SDL3 (SDL3_INCLUDE=<dir>)
# make vectors regenerate app_vectors.tsv from the Sim Configurator checkout SIMCFG (its own interpreter)

all: simmerge-test

ryml.o: $(RYML)/ryml.cc $(RYML)/rapidyaml-0.12.1.hpp
	$(CXX) -o $@ -c -O2 -std=c++17 -I$(RYML) $<

reader.o: $(UI)/xemu-sim-profile-reader.cc $(UI)/xemu-sim-profile-reader.h
	$(CXX) -o $@ -c $(CXXFLAGS) -I$(UI) -I$(RYML) $<

merge.o: $(UI)/xemu-sim-merge.c $(HDRS)
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

merge-drill.o: $(UI)/xemu-sim-merge.c $(HDRS)
	$(CC) -o $@ -c $(CFLAGS) -DSIM_MERGE_DRILL_TRUNC_AIMING -I$(UI) $<

target.o: $(UI)/xemu-sim-target.c $(HDRS)
	$(CC) -o $@ -c $(CFLAGS) -I$(UI) $<

simmerge-test: simmerge-test.cc $(HDRS) merge.o target.o reader.o ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) -I$(RYML) $< merge.o target.o reader.o ryml.o -lm

# the same test with the merge layer and the writers under UBSan, float-cast-overflow included (GCC's
# -fsanitize=undefined leaves it out): any undefined behaviour on a hostile value stops the run
UBSAN=-fsanitize=undefined,float-cast-overflow,float-divide-by-zero -fno-sanitize-recover=all
merge-ub.o: $(UI)/xemu-sim-merge.c $(HDRS)
	$(CC) -o $@ -c $(CFLAGS) $(UBSAN) -I$(UI) $<

target-ub.o: $(UI)/xemu-sim-target.c $(HDRS)
	$(CC) -o $@ -c $(CFLAGS) $(UBSAN) -I$(UI) $<

test-ub.o: simmerge-test.cc $(HDRS)
	$(CXX) -o $@ -c $(CXXFLAGS) $(UBSAN) -I$(UI) -I$(RYML) $<

# linked, run and removed by `make check` (only the objects are kept)
ub-check: test-ub.o merge-ub.o target-ub.o reader.o ryml.o
	$(CXX) -o simmerge-test-ub $(CXXFLAGS) $(UBSAN) test-ub.o merge-ub.o target-ub.o reader.o ryml.o -lm
	@./simmerge-test-ub $(DATA) . > ub.out 2>&1; rc=$$?; last=$$(tail -1 ub.out); errs=$$(grep 'runtime error' ub.out | head -3); \
	 rm -f simmerge-test-ub ub.out; \
	 if [ $$rc -eq 0 ]; then echo "ok   the same checks under UBSan (float-cast-overflow): $$last"; \
	 else echo "$$errs"; echo "FAIL under UBSan (exit $$rc): $$last"; exit 1; fi

drill-trunc: simmerge-test.cc $(HDRS) merge-drill.o target.o reader.o ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) -I$(RYML) $< merge-drill.o target.o reader.o ryml.o -lm

.PHONY: all check drill vectors keys ub-check
check: all
	@cd shared && sha256sum -c --quiet vectors.sha256 && echo "ok   shared/vectors.yaml is the vendored sha256" || { echo "FAIL shared/vectors.yaml is not the vendored file"; exit 1; }
	./simmerge-test $(DATA) .
	@$(MAKE) --no-print-directory ub-check

drill: drill-trunc
	@if ./drill-trunc $(DATA) . > drill-trunc.log 2>&1; then echo "DRILL trunc: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL trunc: RED as expected ($$(grep -c '^FAIL' drill-trunc.log) FAIL lines)"; fi

vectors:
	@test -n "$(SIMCFG)" || { echo "make vectors SIMCFG=<a sim-configurator checkout>"; exit 2; }
	$(SIMCFG)/.venv/bin/python3 gen_app_vectors.py $(SIMCFG) > app_vectors.tsv

keys-check: keys-check.c $(UI)/xemu-sim-keys.h
	@test -n "$(SDL3_INCLUDE)" || { echo "make keys SDL3_INCLUDE=<an SDL3 include dir>"; exit 2; }
	$(CC) -o $@ $(CFLAGS) -I$(UI) -I$(SDL3_INCLUDE) $<

keys: keys-check
	./keys-check $(DATA)/device_keyboard_104.yaml 104 $(DATA)/device_diy_button_box_6.yaml 6
