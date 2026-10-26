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
# The libpq integration test needs only libpq.
PQ_GOALS := pq-test pq-test-asan pq-test-tsan
UI_GOALS := $(if $(MAKECMDGOALS),$(filter-out clean $(PQ_GOALS),$(MAKECMDGOALS)),all)
ifneq ($(UI_GOALS),)
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))
ifeq ($(PKG_LIBS),)
$(error pkg-config cannot find $(PKGS); see examples/explorer/README.md)
endif
endif
ifneq ($(filter $(PQ_GOALS),$(MAKECMDGOALS)),)
PQ_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpq)
PQ_LIBS := $(shell $(PKG_CONFIG) --libs libpq)
ifeq ($(PQ_LIBS),)
$(error pkg-config cannot find libpq; see examples/explorer/README.md)
endif
endif
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/adapters -isystem $(ROOT)/adapters/third_party $(PKG_CFLAGS)
LDLIBS := $(PKG_LIBS) -pthread
BUILD := $(ROOT)/build/explorer
# The worker side: core protocol and machines, the glue that runs them, and the
# libpq, thread and memory adapters.
LINK_SRCS := proto.c conn.c wire.c worker.c link.c
LINK_ADAPTERS := $(ROOT)/adapters/pq.c $(ROOT)/adapters/thread.c $(ROOT)/adapters/mem.c
LINK_HEADERS := proto.h conn.h wire.h worker.h link.h $(ROOT)/adapters/sc_pq.h \
	$(ROOT)/adapters/sc_thread.h $(ROOT)/adapters/sc_mem.h
SRCS := main.c frame.c theme.c view_shell.c view_grid.c input.c $(LINK_SRCS)
ADAPTER := $(ROOT)/adapters/ui.c $(LINK_ADAPTERS)
HEADERS := frame.h theme.h view_shell.h view_grid.h input.h $(LINK_HEADERS) $(ROOT)/adapters/sc_ui.h $(ROOT)/adapters/sc_ui_frame.h $(ROOT)/adapters/third_party/clay.h
FONTS := $(wildcard fonts/*.ttf)

.PHONY: all run screenshot screenshots clean $(PQ_GOALS)

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

# The scenes the plan's visual checks inspect, into build/explorer/scenes/. The
# query and connection scenes (U8) run against a temporary server (pgtest.sh).
SCENES := hello shell-light shell-dark
PG_SCENES := query-rows query-tag query-error query-error-rows conn-auth-failed conn-disconnected
SHOOT = SDL_VIDEO_DRIVER=offscreen $(BUILD)/explorer --scene $$s --screenshot $(BUILD)/scenes/$$s.png || exit 1
screenshots: all
	mkdir -p $(BUILD)/scenes
	for s in $(SCENES); do $(SHOOT); done
	sh pgtest.sh sh -c 'for s in $(PG_SCENES); do $(SHOOT); done'
	@ls $(BUILD)/scenes

# The worker against a temporary Postgres server (plan U7), under ASan and UBSan,
# then under ThreadSanitizer. The sanitizer flags follow SAN_FLAGS and
# TSAN_FLAGS in scripts/sanitize.py; keep them in step.
PQ_TEST := $(ROOT)/tests/explorer_pq_integration.c
PQ_DEPS := $(PQ_TEST) $(LINK_SRCS) $(LINK_HEADERS) $(LINK_ADAPTERS) $(wildcard $(ROOT)/stdlib/*.c)
PQ_BUILD = mkdir -p $(BUILD) && $(CC) -std=c11 -g -O1 -Wall -Wextra -I$(ROOT)/include -I$(ROOT)/adapters \
	$(PQ_CFLAGS) $(PQ_TEST) $(LINK_SRCS) $(LINK_ADAPTERS) $(ROOT)/stdlib/*.c $(PQ_LIBS) -pthread

$(BUILD)/pq_asan: $(PQ_DEPS)
	$(PQ_BUILD) -fsanitize=address,undefined -fno-sanitize-recover=all -o $@

$(BUILD)/pq_tsan: $(PQ_DEPS)
	$(PQ_BUILD) -fsanitize=thread -o $@

pq-test-asan: $(BUILD)/pq_asan
	sh pgtest.sh $(BUILD)/pq_asan

pq-test-tsan: $(BUILD)/pq_tsan
	TSAN_OPTIONS=halt_on_error=1 sh pgtest.sh $(BUILD)/pq_tsan

pq-test: pq-test-asan pq-test-tsan

clean:
	rm -rf $(BUILD)
