CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS ?= -lm
PYTHON ?= python3

all: build/test_decim build/test_reference build/decim_report

build/test_decim: src/decim.c tests/test_decim.c src/decim.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/decim.c tests/test_decim.c $(LDLIBS) -o $@

build/test_reference: src/decim.c tests/test_reference.c src/decim.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/decim.c tests/test_reference.c $(LDLIBS) -o $@

build/decim_report: src/decim.c tools/decim_report.c src/decim.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/decim.c tools/decim_report.c $(LDLIBS) -o $@

test: build/test_decim build/test_reference
	./build/test_decim
	./build/test_reference

# Regenerate results/ from scratch, then re-derive the design from the
# exported coefficients with an independent stdlib-only implementation.
report: build/decim_report
	rm -f results/*.csv
	./build/decim_report results
	$(PYTHON) -I -B tools/verify_results.py results

# Prove the five pre-existing projects are byte-identical and still
# produce identical test output.
regression:
	sh scripts/check_regression.sh

# Unit tests under AddressSanitizer + UndefinedBehaviorSanitizer.
test-sanitize:
	@mkdir -p build
	$(CC) -g -O1 -std=c11 -fsanitize=address,undefined \
	    -fno-sanitize-recover=all src/decim.c tests/test_decim.c \
	    $(LDLIBS) -o build/test_decim_asan
	./build/test_decim_asan
	$(CC) -g -O1 -std=c11 -fsanitize=address,undefined \
	    -fno-sanitize-recover=all src/decim.c tests/test_reference.c \
	    $(LDLIBS) -o build/test_reference_asan
	./build/test_reference_asan

clean:
	rm -rf build results/*.csv

.PHONY: all test report regression test-sanitize clean
