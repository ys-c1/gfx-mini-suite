CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_bitpack

build/test_bitpack: src/bitpack.c tests/test_bitpack.c src/bitpack.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/bitpack.c tests/test_bitpack.c -o $@

test: build/test_bitpack
	./build/test_bitpack

clean:
	rm -rf build

.PHONY: all test clean
