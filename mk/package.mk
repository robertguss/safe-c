# Packaging: libsafec.a, headers, safec.pc and the lint rule pack.
# `make install PREFIX=/usr/local` (DESTDIR is honored for staged installs).
PREFIX ?= /usr/local
VERSION := $(strip $(shell cat VERSION))
PKG_CC := $(if $(filter default,$(origin CC)),clang,$(CC))
PKG_SRCS := $(wildcard stdlib/*.c)
PKG_OBJS := $(patsubst stdlib/%.c,build/pkg/%.o,$(PKG_SRCS))

.PHONY: install uninstall package-test

build/pkg/%.o: stdlib/%.c $(wildcard include/safec/*.h)
	mkdir -p build/pkg
	$(PKG_CC) -std=c11 -O2 -Wall -Wextra -Iinclude -c $< -o $@

build/pkg/libsafec.a: $(PKG_OBJS)
	rm -f $@
	ar rcs $@ $(PKG_OBJS)

install: build/pkg/libsafec.a
	d=$(DESTDIR)$(PREFIX); \
	install -d $$d/include/safec $$d/lib/pkgconfig $$d/bin \
		$$d/share/safec/scripts $$d/share/safec/lint/semgrep; \
	install -m 644 include/safec/*.h $$d/include/safec/; \
	printf '#ifndef SAFEC_VERSION_H\n#define SAFEC_VERSION_H\n#define SAFEC_VERSION "%s"\n#endif\n' '$(VERSION)' > $$d/include/safec/version.h; \
	install -m 644 build/pkg/libsafec.a $$d/lib/; \
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' packaging/safec.pc.in > $$d/lib/pkgconfig/safec.pc; \
	install -m 755 packaging/safec-lint.in $$d/bin/safec-lint; \
	install -m 644 lint/semgrep/subset-v1.yml $$d/share/safec/lint/semgrep/; \
	install -m 644 .clang-tidy $$d/share/safec/; \
	install -m 644 scripts/lint.py scripts/tools.py $$d/share/safec/scripts/; \
	install -m 644 docs/subset-v1.md $$d/share/safec/

uninstall:
	d=$(DESTDIR)$(PREFIX); \
	rm -rf $$d/include/safec $$d/share/safec; \
	rm -f $$d/lib/libsafec.a $$d/lib/pkgconfig/safec.pc $$d/bin/safec-lint

# Installs to build/pkg-prefix, then builds and lints the sample adopter from
# outside the repo with only the installed files.
package-test:
	sh scripts/package-test.sh
