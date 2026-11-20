PYTHON ?= python3

all:
	$(PYTHON) -m py_compile src/clocksync.py src/simulate.py

test: all
	$(PYTHON) -I tests/test_clocksync.py

clean:
	rm -rf src/__pycache__ tests/__pycache__

.PHONY: all test clean
