CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

all: build/test_tone build/test_invert build/test_forward_ab build/bench_invert

build/test_tone: src/tone.cpp tests/test_tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/test_tone.cpp -o $@

build/test_invert: src/tone.cpp tests/test_invert.cpp src/tone.h tests/golden_forward.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/test_invert.cpp -o $@

# Before/after comparison: the original forward implementation (vendored from
# git HEAD) and the current one, linked side by side with renamed symbols.
build/original_tone.o: tests/original_tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -Dbuild_gamma_lut=orig_build -Dapply_lut=orig_apply \
	  -Dlut_is_monotonic=orig_mono -Dlut_mean=orig_mean \
	  -c tests/original_tone.cpp -o $@

build/current_tone.o: src/tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Dbuild_gamma_lut=cur_build -Dapply_lut=cur_apply \
	  -Dlut_is_monotonic=cur_mono -Dlut_mean=cur_mean \
	  -c src/tone.cpp -o $@

build/test_forward_ab: tests/test_forward_ab.cpp build/original_tone.o build/current_tone.o
	$(CXX) $(CXXFLAGS) tests/test_forward_ab.cpp build/original_tone.o build/current_tone.o -o $@

build/bench_invert: src/tone.cpp tests/bench_invert.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/bench_invert.cpp -o $@

test: build/test_tone build/test_invert build/test_forward_ab
	./build/test_tone
	./build/test_invert
	./build/test_forward_ab

bench: build/bench_invert
	./build/bench_invert

clean:
	rm -rf build

.PHONY: all test bench clean
