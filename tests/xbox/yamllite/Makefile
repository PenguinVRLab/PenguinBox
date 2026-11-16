CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -g -std=gnu11
SRC=../../../ui

# make check              the reader (ui/xemu-yaml-lite.c) on the built-in synthetic cases
# make check FILES=...    also parse and dump these files
FILES=

all: yaml-lite-test

yaml-lite-test: yaml-lite-test.c $(SRC)/xemu-yaml-lite.c $(SRC)/xemu-yaml-lite.h
	$(CC) -o $@ $(CFLAGS) -DXEMU_YAML_LITE_STANDALONE -I$(SRC) $< $(SRC)/xemu-yaml-lite.c

.PHONY: all check clean
check: all
	./yaml-lite-test $(FILES)

clean:
	rm -f yaml-lite-test
