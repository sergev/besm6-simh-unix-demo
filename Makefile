# Convenience wrapper around CMake. The build itself is CMakeLists.txt.

BUILD ?= build

# An explicit job count rather than the jobserver: cmake re-execs make, and the
# grandchild does not inherit it.
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

# MAKEFLAGS is cleared so the nested make does not also see this one's
# jobserver and warn about the two ways of being told how to parallelise.
all: $(BUILD)/CMakeCache.txt
	@MAKEFLAGS= cmake --build $(BUILD) --parallel $(JOBS)

$(BUILD)/CMakeCache.txt:
	@cmake -B $(BUILD)

test: all
	@ctest --test-dir $(BUILD) --output-on-failure

clean:
	@rm -rf $(BUILD)

.PHONY: all test clean
