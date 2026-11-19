CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_grid

build/test_grid: src/grid.c tests/test_grid.c src/grid.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grid.c tests/test_grid.c -lm -o $@

test: build/test_grid
	./build/test_grid

clean:
	rm -rf build

.PHONY: all test clean
