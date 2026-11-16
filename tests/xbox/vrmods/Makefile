CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
SRC=../../../hw/xbox/nv2a/pgraph
CXX=g++
CXXFLAGS=-O2 -Wall -Wextra -Werror -g -std=c++17
RYML=../../../thirdparty/rapidyaml
TESTS=vrmods-core-test vrmods-files-test vrmods-replay

# make check      the vr-mods core (vr_mods.c) as it ships, over a fake guest RAM
# make check-fixture DUMP=<pmemsave> TITLE=<hex>   the repo's vr-mods over a real RAM dump (private fixtures)

all: $(TESTS)

vrmods-core-test: vrmods-core-test.c $(SRC)/vr_mods.c $(SRC)/vr_mods.h
	$(CC) -o $@ $(CFLAGS) -I$(SRC) $< $(SRC)/vr_mods.c

# rapidyaml's definitions take ~15 s to compile: built once
ryml.o: $(RYML)/ryml.cc $(RYML)/rapidyaml-0.12.1.hpp
	$(CXX) -o $@ -c -O2 -std=c++17 -I$(RYML) $<

vr_mods.o: $(SRC)/vr_mods.c $(SRC)/vr_mods.h
	$(CC) -o $@ -c $(CFLAGS) -I$(SRC) $<

vrmods-files-test: vrmods-files-test.cc $(SRC)/vr_mods_files.cc $(SRC)/vr_mods.h ryml.o vr_mods.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(SRC) -I$(RYML) $< $(SRC)/vr_mods_files.cc ryml.o vr_mods.o

vr_mods_watch.o: $(SRC)/vr_mods_watch.c $(SRC)/vr_mods_watch.h $(SRC)/vr_worldproj_profiles.h
	$(CC) -o $@ -c $(CFLAGS) -I$(SRC) $<

vr_worldproj_profiles.o: $(SRC)/vr_worldproj_profiles.c $(SRC)/vr_worldproj_profiles.h
	$(CC) -o $@ -c $(CFLAGS) -I$(SRC) $<

vrmods-replay: vrmods-replay.cc $(SRC)/vr_mods_files.cc $(SRC)/vr_mods.h ryml.o vr_mods.o vr_mods_watch.o vr_worldproj_profiles.o
	$(CXX) -o $@ $(CXXFLAGS) -I$(SRC) -I$(RYML) $< $(SRC)/vr_mods_files.cc ryml.o vr_mods.o vr_mods_watch.o vr_worldproj_profiles.o

.PHONY: all check check-fixture clean
check: all
	./vrmods-core-test
	./vrmods-files-test
	./vrmods-replay ../../../vr-mods 2>&1 | tail -1

check-fixture: vrmods-replay
	./vrmods-replay --watches ../../../vr-mods $(TITLE) $(DUMP)

clean:
	rm -f $(TESTS) ryml.o vr_mods.o vr_mods_watch.o vr_worldproj_profiles.o
