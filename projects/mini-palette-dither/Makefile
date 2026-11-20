PYTHON ?= python3

all:
	$(PYTHON) -m py_compile src/palette.py src/subset.py src/demo_images.py

test: all
	$(PYTHON) tests/test_palette.py
	$(PYTHON) tests/test_subset.py

demo: all
	$(PYTHON) tests/run_demo.py

clean:
	rm -rf src/__pycache__ tests/__pycache__ tests/demo_out

.PHONY: all test demo clean
