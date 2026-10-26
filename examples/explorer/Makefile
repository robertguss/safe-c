# Builds the Postgres explorer. Needs SDL3, SDL3_ttf and libpq through pkg-config:
# on Linux `sh scripts/install-sdl3.sh` (run by .agents/setup) puts SDL3 in
# build/tools/sdl3 and libpq-dev supplies libpq; on macOS
# `brew install sdl3 sdl3_ttf libpq pkg-config`. Run from anywhere:
# make -C examples/explorer
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ifeq ($(origin CC),default)
CC := clang
endif
# The caller's PKG_CONFIG_PATH comes first, so the CI image's /opt/sdl3 wins over a
# host build/tools/sdl3 mounted into it. Homebrew's libpq is keg-only, so its .pc
# file is not on the default path.
SC_PKG_PATH := $(if $(PKG_CONFIG_PATH),$(PKG_CONFIG_PATH):)$(ROOT)/build/tools/sdl3/lib/pkgconfig:/opt/homebrew/opt/libpq/lib/pkgconfig:/usr/local/opt/libpq/lib/pkgconfig
# Set per call: make before 4.4 does not pass exported variables to $(shell).
PKG_CONFIG := PKG_CONFIG_PATH=$(SC_PKG_PATH) pkg-config
PKGS := sdl3 sdl3-ttf libpq
ifneq ($(MAKECMDGOALS),clean)
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))
ifeq ($(PKG_LIBS),)
$(error pkg-config cannot find $(PKGS); see examples/explorer/README.md)
endif
endif
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/adapters -isystem $(ROOT)/adapters/third_party $(PKG_CFLAGS)
LDLIBS := $(PKG_LIBS) -pthread
BUILD := $(ROOT)/build/explorer
SRCS := main.c frame.c
ADAPTER := $(ROOT)/adapters/ui.c
HEADERS := frame.h $(ROOT)/adapters/sc_ui.h $(ROOT)/adapters/sc_ui_frame.h $(ROOT)/adapters/third_party/clay.h
FONTS := $(wildcard fonts/*.ttf)

.PHONY: all run screenshot clean

all: $(BUILD)/explorer $(FONTS:%=$(BUILD)/%)

$(BUILD)/explorer: $(SRCS) $(HEADERS) $(ADAPTER) $(wildcard $(ROOT)/stdlib/*.c)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) $(ADAPTER) $(ROOT)/stdlib/*.c $(LDLIBS) -o $@

# The adapter loads fonts from fonts/ next to the binary.
$(BUILD)/fonts/%: fonts/%
	mkdir -p $(BUILD)/fonts
	cp $< $@

run: all
	$(BUILD)/explorer

# Renders SCENE headlessly with the software renderer and saves it (plan KTD15).
SCENE ?= hello
screenshot: all
	SDL_VIDEO_DRIVER=offscreen $(BUILD)/explorer --scene $(SCENE) --screenshot $(BUILD)/$(SCENE).png
	@echo $(BUILD)/$(SCENE).png

clean:
	rm -rf $(BUILD)
