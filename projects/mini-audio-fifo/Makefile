CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_fifo

build/test_fifo: src/fifo.c tests/test_fifo.c src/fifo.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/fifo.c tests/test_fifo.c -lm -o $@

build/bench_fifo: src/fifo.c src/fifo.h bench/bench_fifo_zerocopy.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc src/fifo.c bench/bench_fifo_zerocopy.c -o $@

test: build/test_fifo
	./build/test_fifo

bench: build/bench_fifo
	./build/bench_fifo

clean:
	rm -rf build

.PHONY: all test bench clean
