PYTHON ?= python3

all:
	$(PYTHON) -m py_compile src/palette.py

test: all
	$(PYTHON) tests/test_palette.py

clean:
	rm -rf src/__pycache__ tests/__pycache__

.PHONY: all test clean
