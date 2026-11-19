CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
STACK_FLAGS = -fstack-usage
BLOCK_BYTES = 4096

SRC = src/bitpack.c
TEST = tests/test_bitpack.c
REF = tests/legacy_ref.c
HDR = src/bitpack.h tests/legacy_ref.h

all: build/test_bitpack

build/test_bitpack: $(SRC) $(TEST) $(REF) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(REF) $(TEST) -o $@

# Compile the library alone with -fstack-usage so the implementation's real
# stack footprint can be checked against the one-block bound.
build/bitpack_stack.o: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(STACK_FLAGS) -c $(SRC) -o $@

test: build/test_bitpack build/bitpack_stack.o
	./build/test_bitpack
	@echo
	@sh tests/check_stack.sh build/bitpack_stack.su $(BLOCK_BYTES)

clean:
	rm -rf build

.PHONY: all test clean
