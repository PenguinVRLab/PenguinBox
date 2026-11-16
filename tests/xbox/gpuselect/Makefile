CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
SRC=../../../hw/xbox/nv2a/pgraph/vk

# make check      the GPU selection policy (gpu_select.c) as it ships

all: gpuselect-test

gpuselect-test: gpuselect-test.c $(SRC)/gpu_select.c $(SRC)/gpu_select.h
	$(CC) -o $@ $(CFLAGS) -I$(SRC) $< $(SRC)/gpu_select.c

.PHONY: all check clean
check: all
	./gpuselect-test | tail -1
	./gpuselect-test > /dev/null

clean:
	rm -f gpuselect-test
