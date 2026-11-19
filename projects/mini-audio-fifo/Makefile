CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_fifo

build/test_fifo: src/fifo.c tests/test_fifo.c src/fifo.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/fifo.c tests/test_fifo.c -lm -o $@

test: build/test_fifo
	./build/test_fifo

clean:
	rm -rf build

.PHONY: all test clean
