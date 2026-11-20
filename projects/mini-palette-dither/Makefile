PYTHON ?= python3

all:
	$(PYTHON) -m py_compile src/palette.py src/bluenoise.py

test: all
	$(PYTHON) tests/test_palette.py
	$(PYTHON) tests/test_bluenoise.py

demo: all
	$(PYTHON) demo_gradients.py

clean:
	rm -rf src/__pycache__ tests/__pycache__

.PHONY: all test demo clean
