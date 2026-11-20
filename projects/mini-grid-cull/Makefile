CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_grid build/test_lgrid

build/test_grid: src/grid.c tests/test_grid.c src/grid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c tests/test_grid.c -lm -o $@

build/test_lgrid: src/grid.c src/lgrid.c tests/test_lgrid.c src/grid.h src/lgrid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c src/lgrid.c tests/test_lgrid.c -lm -o $@

build/bench_lgrid: src/grid.c src/lgrid.c tests/bench_lgrid.c src/grid.h src/lgrid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c src/lgrid.c tests/bench_lgrid.c -lm -o $@

test: build/test_grid build/test_lgrid
	./build/test_grid
	./build/test_lgrid

bench: build/bench_lgrid
	./build/bench_lgrid

clean:
	rm -rf build

.PHONY: all test bench clean
