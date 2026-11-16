CXX=g++
CXXFLAGS=-O2 -Wall -Wextra -Werror -g -std=c++17
UI=../../../ui
RYML=../../../thirdparty/rapidyaml
DATA=../../../data/sim-profiles
SCRATCH?=scratch

# make check   the reader as it ships, over the app's shipped data and good fixtures, plus one mutation per rule
# make drill   the RED drills: the pair rules (THE RULE and its family) stubbed to accept, and rapidyaml's default
#              (aborting) handler; each must make the test FAIL (the target fails if either passes)

all: simprofile-reader-test

# rapidyaml's definitions take ~15 s to compile: built once
ryml.o: $(RYML)/ryml.cc $(RYML)/rapidyaml-0.12.1.hpp
	$(CXX) -o $@ -c -O2 -std=c++17 -I$(RYML) $<

simprofile-reader-test: simprofile-reader-test.cc $(UI)/xemu-sim-profile-reader.cc $(UI)/xemu-sim-profile-reader.h ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(UI) -I$(RYML) $< $(UI)/xemu-sim-profile-reader.cc ryml.o

drill-accept: simprofile-reader-test.cc $(UI)/xemu-sim-profile-reader.cc ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -DSIM_PROFILE_READER_DRILL_ACCEPT_PAIRS -I$(UI) -I$(RYML) $< $(UI)/xemu-sim-profile-reader.cc ryml.o

drill-abort: simprofile-reader-test.cc $(UI)/xemu-sim-profile-reader.cc ryml.o
	$(CXX) -o $@ $(CXXFLAGS) -DSIM_PROFILE_READER_DRILL_DEFAULT_CALLBACKS -I$(UI) -I$(RYML) $< $(UI)/xemu-sim-profile-reader.cc ryml.o

.PHONY: all check drill
check: all
	mkdir -p $(SCRATCH)
	@awk -F'\t' 'NR>1{print $$4"  "$$2}' bad/MANIFEST.tsv | (cd good && sha256sum -c --quiet -) && echo "ok   the bad rows match the vendored good files" || { echo "FAIL a bad row is stale (good_sha256)"; exit 1; }
	./simprofile-reader-test $(DATA) good $(SCRATCH)

drill: drill-accept drill-abort
	mkdir -p $(SCRATCH)
	@if ./drill-accept $(DATA) good $(SCRATCH) > drill-accept.log 2>&1; then echo "DRILL accept: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL accept: RED as expected ($$(grep -c '^FAIL' drill-accept.log) FAIL lines)"; fi
	@./drill-abort $(DATA) good $(SCRATCH) > drill-abort.log 2>&1; rc=$$?; \
	 if [ $$rc -eq 0 ]; then echo "DRILL abort: the test PASSED (no power)"; exit 1; \
	 else echo "DRILL abort: RED as expected (exit $$rc; last line: $$(tail -1 drill-abort.log | cut -c1-80))"; fi
