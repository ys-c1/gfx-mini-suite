PYTHON ?= python3
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PACKDIR := ../mini-bitplane-pack

all:
	$(PYTHON) -m py_compile src/palette.py src/dither.py
	@mkdir -p build
	$(CC) $(CFLAGS) tools/bitpack_cli.c $(PACKDIR)/src/bitpack.c -o build/bitpack_cli

test: all
	$(PYTHON) tests/test_palette.py
	$(PYTHON) tests/test_dither.py
	$(PYTHON) tests/test_pngio.py
	$(PYTHON) tests/test_integration.py

samples: all
	$(PYTHON) tools/render_samples.py samples

bench: samples
	$(PYTHON) tools/bench_dither.py --json samples/report.json

clean:
	rm -rf build src/__pycache__ tests/__pycache__ tools/__pycache__

.PHONY: all test samples bench clean
