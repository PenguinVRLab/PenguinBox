CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
SRC=../../../hw/xbox/nv2a/pgraph

# make check      the stereo promotion rule (vr_stereo_promo.c) as it ships

all: stereopromo-test

stereopromo-test: stereopromo-test.c $(SRC)/vr_stereo_promo.c $(SRC)/vr_stereo_promo.h
	$(CC) -o $@ $(CFLAGS) -I$(SRC) $< $(SRC)/vr_stereo_promo.c

.PHONY: all check clean
check: all
	./stereopromo-test | tail -1
	./stereopromo-test > /dev/null

clean:
	rm -f stereopromo-test
