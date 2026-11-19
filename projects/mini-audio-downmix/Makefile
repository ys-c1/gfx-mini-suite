CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
# CLOCK_MONOTONIC for the timing harness only; harmless elsewhere.
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L

all: build/test_downmix build/downmix_report

build/test_downmix: src/downmix.c tests/test_downmix.c src/downmix.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/downmix.c tests/test_downmix.c -lm -o $@

build/downmix_report: src/downmix.c tools/downmix_report.c tools/wav.c \
                      tools/wav.h src/downmix.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/downmix.c tools/downmix_report.c \
	      tools/wav.c -lm -o $@

test: build/test_downmix
	./build/test_downmix

report: build/downmix_report
	./build/downmix_report gen .
	./build/downmix_report run .
	python3 -I tools/verify_results.py

# Proves the other four/five projects are untouched, against the
# committed tree. Needs a writable scratch directory.
regression:
	scripts/check_regression.sh

# Rebuilds the library with AddressSanitizer + UBSan and runs the tests.
# Kept separate so the normal target stays at -O2.
test-sanitize:
	@mkdir -p build
	$(CC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined \
	      -fno-omit-frame-pointer -Wall -Wextra -std=c11 src/downmix.c \
	      tests/test_downmix.c -lm -o build/test_downmix_san
	./build/test_downmix_san

clean:
	rm -rf build

.PHONY: all test report regression test-sanitize clean
