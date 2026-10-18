# Sample adopter. Run `make install PREFIX=...` in the Safe C tree first, then
#   PATH=$PREFIX/bin:$PATH PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig make -C examples/adopter lint all
CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
PKG := $(shell pkg-config --cflags --libs safec)

all: adopter
	./adopter

adopter: main.c adapters/storage.c adapters/storage.h
	$(CC) $(CFLAGS) main.c adapters/storage.c $(PKG) -o $@

.PHONY: all lint clean
lint:
	safec-lint main.c

clean:
	rm -f adopter
