LLVM ?= $(firstword $(wildcard /opt/homebrew/opt/llvm /usr/local/opt/llvm))
ifneq ($(LLVM),)
  export PATH := $(LLVM)/bin:$(PATH)
endif

ifeq ($(origin CC),default)
  ifneq ($(LLVM),)
    CC := $(LLVM)/bin/clang
  else
    CC := $(shell command -v clang >/dev/null 2>&1 && echo clang || echo clang-18)
  endif
endif

CFLAGS ?= -std=c11 -Werror -Wall -Wextra -Wvla -Wconversion -Wsign-conversion -g -O1
SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all
FUZZ_SECONDS ?= 60

.PHONY: lint lint-selftest test fuzz gate-selftest gates

lint:
	python3 scripts/lint.py $(LINT_SRC)

lint-selftest:
	python3 scripts/lint.py --selftest

test:
	@if ! command -v "$(CC)" >/dev/null 2>&1; then \
		echo "error: missing required tool: $(CC) (set CC or LLVM to a clang toolchain with ASan/UBSan)" >&2; \
		exit 2; \
	fi; \
	probe_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/safe-c-san-probe.XXXXXX"); \
	trap 'rm -rf "$$probe_dir"' EXIT INT TERM; \
	printf 'int main(void) { return 0; }\n' >"$$probe_dir/probe.c"; \
	if ! "$(CC)" -std=c11 $(SAN_FLAGS) "$$probe_dir/probe.c" -o "$$probe_dir/probe" >"$$probe_dir/probe.log" 2>&1; then \
		cat "$$probe_dir/probe.log" >&2; \
		echo "error: $(CC) is missing required toolchain support: AddressSanitizer/UndefinedBehaviorSanitizer ($(SAN_FLAGS))" >&2; \
		exit 2; \
	fi; \
	tests=""; \
	if [ -d tests ]; then \
		tests=$$(find tests -type f -name '*_test.c' | sort); \
	fi; \
	if [ -z "$$tests" ]; then \
		echo "test: no *_test.c sources"; \
		exit 0; \
	fi; \
	for src in $$tests; do \
		out="build/$${src%.c}"; \
		mkdir -p "$$(dirname "$$out")"; \
		"$(CC)" $(CFLAGS) $(SAN_FLAGS) "$$src" -o "$$out" || exit 1; \
		"$$out" || exit 1; \
		echo "pass $$src"; \
	done

fuzz:
	CC="$(CC)" LLVM="$(LLVM)" FUZZ_SECONDS="$(FUZZ_SECONDS)" sh scripts/fuzz-all.sh

gate-selftest:
	@if ! command -v "$(CC)" >/dev/null 2>&1; then \
		echo "error: missing required tool: $(CC) (set CC or LLVM to a clang toolchain with ASan/UBSan/libFuzzer)" >&2; \
		exit 2; \
	fi; \
	mkdir -p build/gates; \
	if ! "$(CC)" $(CFLAGS) $(SAN_FLAGS) tests/gates/asan-canary.c -o build/gates/asan-canary >build/gates/asan-build.log 2>&1; then \
		cat build/gates/asan-build.log >&2; \
		echo "error: failed to build tests/gates/asan-canary.c with $(CC) $(SAN_FLAGS)" >&2; \
		exit 2; \
	fi; \
	if ASAN_OPTIONS=abort_on_error=0:exitcode=1 build/gates/asan-canary >build/gates/asan-canary.log 2>&1; then \
		cat build/gates/asan-canary.log >&2; \
		echo "error: asan canary was not caught" >&2; \
		exit 1; \
	fi; \
	if ! grep -q "AddressSanitizer" build/gates/asan-canary.log; then \
		cat build/gates/asan-canary.log >&2; \
		echo "error: asan canary failed without AddressSanitizer report" >&2; \
		exit 1; \
	fi; \
	echo "asan canary caught"; \
	if ! "$(CC)" $(CFLAGS) $(SAN_FLAGS) tests/gates/ubsan-canary.c -o build/gates/ubsan-canary >build/gates/ubsan-build.log 2>&1; then \
		cat build/gates/ubsan-build.log >&2; \
		echo "error: failed to build tests/gates/ubsan-canary.c with $(CC) $(SAN_FLAGS)" >&2; \
		exit 2; \
	fi; \
	if UBSAN_OPTIONS=abort_on_error=0:exitcode=1 build/gates/ubsan-canary >build/gates/ubsan-canary.log 2>&1; then \
		cat build/gates/ubsan-canary.log >&2; \
		echo "error: ubsan canary was not caught" >&2; \
		exit 1; \
	fi; \
	if ! grep -Eq "UndefinedBehaviorSanitizer|runtime error:" build/gates/ubsan-canary.log; then \
		cat build/gates/ubsan-canary.log >&2; \
		echo "error: ubsan canary failed without UndefinedBehaviorSanitizer report" >&2; \
		exit 1; \
	fi; \
	echo "ubsan canary caught"; \
	rc=0; \
	CC="$(CC)" LLVM="$(LLVM)" FUZZ_SECONDS=10 sh scripts/fuzz-all.sh fuzz/canary_fuzz.c >build/gates/fuzz-canary.log 2>&1 || rc=$$?; \
	if [ "$$rc" -eq 2 ]; then \
		cat build/gates/fuzz-canary.log >&2; \
		exit 2; \
	fi; \
	if [ "$$rc" -eq 0 ]; then \
		cat build/gates/fuzz-canary.log >&2; \
		echo "error: fuzz canary was not caught" >&2; \
		exit 1; \
	fi; \
	if ! grep -Eq "libFuzzer|AddressSanitizer|UndefinedBehaviorSanitizer" build/gates/fuzz-canary.log; then \
		cat build/gates/fuzz-canary.log >&2; \
		echo "error: fuzz canary failed without libFuzzer/sanitizer report" >&2; \
		exit 1; \
	fi; \
	echo "fuzz canary caught"

# Lint, sanitizer tests, and waiver expiry today. Frama-C WP (SC-04) joins later.
gates: lint test
	python3 scripts/lint.py --waivers
