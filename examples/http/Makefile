# Builds the HTTP demo. Needs libmicrohttpd (libmicrohttpd-dev on Debian/Ubuntu,
# `brew install libmicrohttpd` on macOS). Run from anywhere: make -C examples/http
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ifeq ($(origin CC),default)
CC := clang
endif
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
LDLIBS := $(shell pkg-config --libs libmicrohttpd 2>/dev/null || echo -lmicrohttpd) -pthread
CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/adapters $(shell pkg-config --cflags libmicrohttpd 2>/dev/null)
BUILD := $(ROOT)/build/http

.PHONY: all run smoke clean

all: $(BUILD)/server

$(BUILD)/server: server.c parse.c parse.h $(ROOT)/adapters/microhttpd.c $(ROOT)/adapters/sc_microhttpd.h $(wildcard $(ROOT)/stdlib/*.c)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) server.c parse.c $(ROOT)/adapters/microhttpd.c $(ROOT)/stdlib/*.c $(LDLIBS) -o $@

run: all
	$(BUILD)/server

# Starts the server on a spare port, checks GET /, and stops it.
smoke: all
	sh smoke.sh $(BUILD)/server

clean:
	rm -rf $(BUILD)
