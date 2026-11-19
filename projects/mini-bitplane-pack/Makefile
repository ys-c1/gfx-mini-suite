CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_bitpack build/test_bitpack_at

build/test_bitpack: src/bitpack.c tests/test_bitpack.c src/bitpack.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/bitpack.c tests/test_bitpack.c -o $@

build/test_bitpack_at: src/bitpack.c tests/test_bitpack_at.c src/bitpack.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/bitpack.c tests/test_bitpack_at.c -o $@

test: build/test_bitpack build/test_bitpack_at
	./build/test_bitpack
	./build/test_bitpack_at

clean:
	rm -rf build

.PHONY: all test clean
