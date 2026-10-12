CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

.PHONY: all test
all: build/cache_sim build/cache_tests

build:
	mkdir -p build

build/cache_sim: src/main.cpp include/cache.hpp include/trace.hpp | build
	$(CXX) $(CXXFLAGS) -Iinclude src/main.cpp -o $@

build/cache_tests: tests/test_cache.cpp include/cache.hpp include/trace.hpp | build
	$(CXX) $(CXXFLAGS) -Iinclude tests/test_cache.cpp -o $@

test: all
	./build/cache_tests
	python3 tests/test_integration.py --binary ./build/cache_sim
