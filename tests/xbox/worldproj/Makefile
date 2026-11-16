CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
SRC=../../../hw/xbox/nv2a/pgraph
TESTS=worldproj-math-test worldproj-sidecar-test worldproj-setview-test worldproj-hmd-test worldproj-hmd-sidecar-test \
	worldproj-config-test worldproj-2d-test worldproj-slave-test

# make check                  the committed synthetic pins
# make check PINS=<file>      also a title's own pins
PINS=

all: $(TESTS)

worldproj-math-test: worldproj-math-test.c pins.h $(SRC)/vr_worldproj_math.h
	$(CC) -o $@ $(CFLAGS) $< -lm

worldproj-hmd-test: worldproj-hmd-test.c $(SRC)/vr_worldproj_math.h $(SRC)/vr_worldproj_stamp.h
	$(CC) -o $@ $(CFLAGS) $< -lm

# the emulator's own source, built against stub headers (stubs/) and a fake of what it reads from outside the
# renderer (fake_host.c: the config's view, the title id)
MODULE=$(SRC)/vr_worldproj.c $(SRC)/vr_worldproj_profiles.c fake_host.c
worldproj-sidecar-test worldproj-setview-test worldproj-hmd-sidecar-test worldproj-config-test worldproj-2d-test worldproj-slave-test: %: %.c pins.h fake_host.h $(MODULE) $(SRC)/vr_worldproj.h $(SRC)/vr_worldproj_math.h $(SRC)/vr_worldproj_stamp.h $(SRC)/vr_worldproj_host.h $(SRC)/vr_worldproj_profiles.h
	$(CC) -o $@ $(CFLAGS) -Istubs -I$(SRC) $< $(MODULE) -lm

# worldproj-config-test also compiles in test_rows.inc: a row whose every number differs from Halo's and the defaults
worldproj-config-test: test_rows.inc
worldproj-config-test: CFLAGS += -DVR_WP_PROFILES_EXTRA='"$(CURDIR)/test_rows.inc"'

.PHONY: all check clean
check: all
	./worldproj-math-test pins-synthetic.txt $(PINS)
	@ctl=$$(mktemp); XEMU_VR_WORLDPROJ=$$ctl ./worldproj-sidecar-test pins-synthetic.txt 2>/dev/null; r=$$?; rm -f $$ctl; exit $$r
	@env -u XEMU_VR_WORLDPROJ ./worldproj-setview-test pins-synthetic.txt 2>/dev/null
	./worldproj-hmd-test
	@ctl=$$(mktemp); XEMU_VR_WORLDPROJ=$$ctl ./worldproj-hmd-sidecar-test pins-synthetic.txt 2>/dev/null; r=$$?; rm -f $$ctl; exit $$r
	@env -u XEMU_VR_WORLDPROJ ./worldproj-config-test config
	@ctl=$$(mktemp); XEMU_VR_WORLDPROJ=$$ctl ./worldproj-config-test ctl; r=$$?; rm -f $$ctl; exit $$r
	@ctl=$$(mktemp); XEMU_VR_WORLDPROJ=$$ctl ./worldproj-2d-test; r=$$?; rm -f $$ctl; exit $$r
	@ctl=$$(mktemp); XEMU_VR_WORLDPROJ=$$ctl ./worldproj-slave-test; r=$$?; rm -f $$ctl; exit $$r

clean:
	rm -f $(TESTS)
