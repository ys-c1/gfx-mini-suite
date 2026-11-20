PYTHON ?= python3

all:
	$(PYTHON) -m py_compile src/palette.py src/dither.py tools/bench_dither.py

test: all
	cd tests && $(PYTHON) run.py test_palette test_dither
	$(PYTHON) tests/check_unchanged.py

bench: all
	$(PYTHON) tools/bench_dither.py

clean:
	rm -rf src/__pycache__ tests/__pycache__ tools/__pycache__

.PHONY: all test bench clean
