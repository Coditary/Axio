LANGUAGE_TEST=tests/language
BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
LIT ?= lit

.PHONY: help configure build rebuild check test test-unit test-lit test-all docs clean coverage lint format

help:
	@printf "Targets:\n"
	@printf "  configure   Configure CMake build\n"
	@printf "  build       Build compiler and unit tests\n"
	@printf "  rebuild     Clean configure and build\n"
	@printf "  check       Build and run all tests\n"
	@printf "  test-unit   Run CTest unit tests\n"
	@printf "  test-lit    Run lit integration tests\n"
	@printf "  test-all    Run unit tests and lit suite\n"
	@printf "  docs        Generate API docs with Doxygen\n"
	@printf "  lint        Run available static checks\n"
	@printf "  format      Show formatting tool status\n"
	@printf "  coverage    Build with coverage and print llvm-cov report\n"
	@printf "  clean       Remove build output\n"

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j4

rebuild: clean build

test-unit: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

test-lit: build
	$(LIT) $(LANGUAGE_TEST)/codegen $(LANGUAGE_TEST)/generated $(LANGUAGE_TEST)/parse $(LANGUAGE_TEST)/runtime $(LANGUAGE_TEST)/sema $(LANGUAGE_TEST)/spec

docs:
	@if command -v doxygen >/dev/null 2>&1; then \
		doxygen Doxyfile; \
		printf "API docs written to docs/api/html/index.html and docs/api/xml/\n"; \
	else \
		printf "doxygen not installed. Install it and rerun 'make docs'.\n"; \
		exit 1; \
	fi

test: test-unit test-lit clean-tests

check: test-all

lint: build
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy src/**/*.cpp include/**/*.h -- -Iinclude; \
	else \
		printf "clang-tidy not installed; skipping static lint.\n"; \
	fi

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		printf "clang-format is available: %s\n" "$$(command -v clang-format)"; \
	else \
		printf "clang-format not installed.\n"; \
	fi

coverage:
	$(CMAKE) -S . -B $(BUILD_DIR)-coverage -DAXC_ENABLE_COVERAGE=ON
	$(CMAKE) --build $(BUILD_DIR)-coverage -j4
	$(CTEST) --test-dir $(BUILD_DIR)-coverage --output-on-failure
	$(LIT) tests/runtime tests/parse tests/sema tests/codegen tests/spec
	@llvm-cov gcov $(BUILD_DIR)-coverage/CMakeFiles/axc_core.dir/src/Sema/Sema.cpp.o >/dev/null 2>&1 || true
	@printf "Coverage artifacts generated in %s\n" "$(BUILD_DIR)-coverage"

clean:
	rm -rf $(BUILD_DIR) $(BUILD_DIR)-coverage

clean-tests:
	@printf "Cleaning up test Output directories...\n"
	@find tests -type d -name "Output" -exec rm -rf {} +
	@printf "Test artifacts removed.\n"
