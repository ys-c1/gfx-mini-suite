CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

all: build/test_tone

build/test_tone: src/tone.cpp tests/test_tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/test_tone.cpp -o $@

test: build/test_tone
	./build/test_tone

clean:
	rm -rf build

.PHONY: all test clean
