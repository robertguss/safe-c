.PHONY: lint lint-selftest gates

lint:
	python3 scripts/lint.py $(LINT_SRC)

lint-selftest:
	python3 scripts/lint.py --selftest

# Lint only today. Sanitizer tests (SC-03) and Frama-C WP (SC-04) join later.
gates: lint
	python3 scripts/lint.py --waivers
