CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
SRC=../../../ui

# make check    the reader (ui/xemu-disc-xbe.c) on synthetic images built by the test
all: disc-xbe-test

disc-xbe-test: disc-xbe-test.c $(SRC)/xemu-disc-xbe.c $(SRC)/xemu-disc-xbe.h
	$(CC) -o $@ $(CFLAGS) -DXEMU_DISC_XBE_STANDALONE -I$(SRC) $< $(SRC)/xemu-disc-xbe.c

.PHONY: all check clean
check: all
	./disc-xbe-test

clean:
	rm -f disc-xbe-test
