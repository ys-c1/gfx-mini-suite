CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

all: build/test_tone

build/test_tone: src/tone.cpp tests/test_tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/tone.cpp tests/test_tone.cpp -o $@

build/gen_samples: tools/gen_samples.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tools/gen_samples.cpp -o $@

build/report_match: tools/report_match.cpp src/tone.cpp src/tone.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tools/report_match.cpp src/tone.cpp -o $@

data: build/gen_samples
	@mkdir -p data
	./build/gen_samples

test: build/test_tone
	./build/test_tone

report: build/report_match data
	./build/report_match

clean:
	rm -rf build

.PHONY: all test report data clean
