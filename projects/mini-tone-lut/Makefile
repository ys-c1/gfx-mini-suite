CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

all: build/test_tone build/test_spline build/bench_tone

build/test_tone: src/tone.cpp tests/test_tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/test_tone.cpp -o $@

build/test_spline: src/tone.cpp tests/test_spline.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/test_spline.cpp -o $@

build/bench_tone: src/tone.cpp tests/bench_tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/bench_tone.cpp -o $@

test: build/test_tone build/test_spline
	./build/test_tone
	./build/test_spline

bench: build/bench_tone
	./build/bench_tone

clean:
	rm -rf build

.PHONY: all test bench clean
