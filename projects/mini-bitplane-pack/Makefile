CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_bitpack build/test_planar

build/test_bitpack: src/bitpack.c tests/test_bitpack.c src/bitpack.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/bitpack.c tests/test_bitpack.c -o $@

build/test_planar: src/bitpack.c src/planar.c tests/test_planar.c src/bitpack.h src/planar.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/bitpack.c src/planar.c tests/test_planar.c -o $@

build/bench_planar: src/bitpack.c src/planar.c tests/bench_planar.c src/bitpack.h src/planar.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/bitpack.c src/planar.c tests/bench_planar.c -o $@

test: build/test_bitpack build/test_planar
	./build/test_bitpack
	./build/test_planar

bench: build/bench_planar
	./build/bench_planar

clean:
	rm -rf build

.PHONY: all test bench clean
