CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS ?= -lm

all: build/test_grid build/test_adaptive build/bench_adaptive

build/test_grid: src/grid.c tests/test_grid.c src/grid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c tests/test_grid.c $(LDLIBS) -o $@

build/test_adaptive: src/grid.c tests/test_adaptive.c src/grid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c tests/test_adaptive.c $(LDLIBS) -o $@

build/bench_adaptive: src/grid.c tests/bench_adaptive.c src/grid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c tests/bench_adaptive.c $(LDLIBS) -o $@

test: build/test_grid build/test_adaptive
	./build/test_grid
	./build/test_adaptive

bench: build/bench_adaptive
	./build/bench_adaptive

clean:
	rm -rf build

.PHONY: all test bench clean
