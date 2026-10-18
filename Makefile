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

# Everything the Gates workflow runs, in parallel; CI_STEPS picks a subset.
# ci-docker runs it in the pinned toolchain from ci/Dockerfile: the image CI
# publishes for this Dockerfile when it exists (x86_64 only), else a local build.
.PHONY: ci ci-docker
CI_IMAGE ?= ghcr.io/robertguss/safe-c-ci:$(shell python3 scripts/ci.py --image-tag)

ci:
	python3 scripts/ci.py $(CI_STEPS)

ci-docker:
	docker image inspect $(CI_IMAGE) >/dev/null 2>&1 \
		|| { [ "$$(uname -m)" = x86_64 ] && docker pull $(CI_IMAGE); } \
		|| docker build -t $(CI_IMAGE) ci
	docker run --rm --user "$$(id -u):$$(id -g)" -e CI_JOBS -e FUZZ_SECONDS -v "$(CURDIR):/src" \
		$(CI_IMAGE) python3 scripts/ci.py $(CI_STEPS)
