# Pass explicit overrides through without exporting Make's built-in CC=cc.
ifeq ($(origin CC),default)
unexport CC
else
export CC
endif
export LLVM CFLAGS FUZZ_SECONDS MODEL_QUEUE_SIZES TLA2TOOLS

.PHONY: lint lint-selftest test tsan fuzz prove model waivers gate-selftest gates bench

lint:
	python3 scripts/lint.py $(LINT_SRC)

lint-selftest:
	python3 scripts/lint.py --selftest

test tsan fuzz:
	python3 scripts/sanitize.py $@

prove:
	sh scripts/prove.sh $(PROVE_SRC)

model:
	python3 scripts/model.py

waivers:
	python3 scripts/waivers-report.py

# Perf numbers are recorded, not gated: -O2, no sanitizers.
bench:
	mkdir -p build
	for src in tests/bench/*_bench.c; do \
		out=build/$$(basename $$src .c); \
		$${CC:-clang} -std=c11 -O2 -Iinclude $$src stdlib/*.c -pthread -o $$out && ./$$out || exit 1; \
	done

gate-selftest:
	python3 scripts/sanitize.py gate-selftest
	sh scripts/prove.sh --selftest

# Keep lint first even with make -j; the wrapper runs sanitizer stages in order.
gates: lint
	python3 scripts/sanitize.py gates
	sh scripts/prove.sh $(PROVE_SRC)
	python3 scripts/model.py

include mk/package.mk
