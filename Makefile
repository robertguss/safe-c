# Pass explicit overrides through without exporting Make's built-in CC=cc.
ifeq ($(origin CC),default)
unexport CC
else
export CC
endif
export LLVM CFLAGS FUZZ_SECONDS

.PHONY: lint lint-selftest test fuzz gate-selftest gates

lint:
	python3 scripts/lint.py $(LINT_SRC)

lint-selftest:
	python3 scripts/lint.py --selftest

test fuzz gate-selftest:
	python3 scripts/sanitize.py $@

# Keep lint first even with make -j; the wrapper runs sanitizer stages in order.
gates: lint
	python3 scripts/sanitize.py gates
	python3 scripts/lint.py --waivers
