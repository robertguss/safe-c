# Builds the Postgres explorer. Needs SDL3, SDL3_ttf and libpq through pkg-config:
# on Linux `sh scripts/install-sdl3.sh` (run by .agents/setup) puts SDL3 in
# build/tools/sdl3 and libpq-dev supplies libpq; on macOS
# `brew install sdl3 sdl3_ttf libpq pkg-config`. Run from anywhere:
# make -C examples/explorer
#
# `sh scripts/install-libpq.sh` (also run by .agents/setup) builds libpq 18 into
# build/tools/libpq18 (plan KTD20). When it is there the explorer links it, with
# an rpath, so Linux runs the Mac's libpq 17+ path (the non-blocking
# cancel); `pq-test` stays on the system libpq and `pq-test-18`
# runs the same test against libpq 18.
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
LIBPQ18 ?= $(ROOT)/build/tools/libpq18
PQ18_PC := $(wildcard $(LIBPQ18)/lib/pkgconfig/libpq.pc)
PQ18_CONFIG := PKG_CONFIG_PATH=$(LIBPQ18)/lib/pkgconfig pkg-config
# The libpq integration test needs only libpq.
PQ_GOALS := pq-test pq-test-asan pq-test-tsan
PQ18_GOALS := pq-test-18 pq-test-18-asan pq-test-18-tsan
UI_GOALS := $(if $(MAKECMDGOALS),$(filter-out clean $(PQ_GOALS) $(PQ18_GOALS),$(MAKECMDGOALS)),all)
ifneq ($(UI_GOALS),)
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 sdl3-ttf) \
	$(if $(PQ18_PC),$(shell $(PQ18_CONFIG) --cflags libpq),$(shell $(PKG_CONFIG) --cflags libpq))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))
ifeq ($(PKG_LIBS),)
$(error pkg-config cannot find $(PKGS); see examples/explorer/README.md)
endif
ifneq ($(PQ18_PC),)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs sdl3 sdl3-ttf) $(shell $(PQ18_CONFIG) --libs libpq) \
	-Wl,-rpath,$(LIBPQ18)/lib
endif
endif
ifneq ($(filter $(PQ_GOALS),$(MAKECMDGOALS)),)
PQ_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpq)
PQ_LIBS := $(shell $(PKG_CONFIG) --libs libpq)
ifeq ($(PQ_LIBS),)
$(error pkg-config cannot find libpq; see examples/explorer/README.md)
endif
endif
ifneq ($(filter $(PQ18_GOALS),$(MAKECMDGOALS)),)
ifeq ($(PQ18_PC),)
$(error no libpq 18 in $(LIBPQ18); run sh scripts/install-libpq.sh)
endif
PQ18_CFLAGS := $(shell $(PQ18_CONFIG) --cflags libpq)
PQ18_LIBS := $(shell $(PQ18_CONFIG) --libs libpq) -Wl,-rpath,$(LIBPQ18)/lib
endif
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/adapters -isystem $(ROOT)/adapters/third_party $(PKG_CFLAGS)
LDLIBS := $(PKG_LIBS) -pthread
BUILD := $(ROOT)/build/explorer
# The worker side: core protocol and machines, the glue that runs them, and the
# libpq, thread and memory adapters.
LINK_SRCS := proto.c budget.c conn.c result.c wire.c worker.c link.c
LINK_ADAPTERS := $(ROOT)/adapters/pq.c $(ROOT)/adapters/thread.c $(ROOT)/adapters/mem.c
LINK_HEADERS := proto.h budget.h conn.h result.h wire.h worker.h link.h $(ROOT)/adapters/sc_pq.h \
	$(ROOT)/adapters/sc_thread.h $(ROOT)/adapters/sc_mem.h
SRCS := main.c frame.c focus.c theme.c view_shell.c view_grid.c query_text.c input.c grid.c detail.c tabs.c $(LINK_SRCS)
ADAPTER := $(ROOT)/adapters/ui.c $(LINK_ADAPTERS)
HEADERS := frame.h focus.h theme.h view_shell.h view_grid.h query_text.h input.h grid.h detail.h tabs.h $(LINK_HEADERS) $(ROOT)/adapters/sc_ui.h $(ROOT)/adapters/sc_ui_frame.h $(ROOT)/adapters/third_party/clay.h
FONTS := $(wildcard fonts/*.ttf)

.PHONY: all run screenshot screenshots clean $(PQ_GOALS) $(PQ18_GOALS)

all: $(BUILD)/explorer $(FONTS:%=$(BUILD)/%)

$(BUILD)/explorer: $(SRCS) $(HEADERS) $(ADAPTER) $(wildcard $(ROOT)/stdlib/*.c) $(PQ18_PC)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SRCS) $(ADAPTER) $(ROOT)/stdlib/*.c $(LDLIBS) -o $@

# The adapter loads fonts from fonts/ next to the binary.
$(BUILD)/fonts/%: fonts/%
	mkdir -p $(BUILD)/fonts
	cp $< $@

run: all
	$(BUILD)/explorer

# The scenes the plan's visual checks inspect. The query and connection scenes
# (U8) run against a temporary server (pgtest.sh).
SCENES := hello shell-light shell-dark
PG_SCENES := query-rows query-wide query-tag query-error query-error-rows conn-auth-failed conn-disconnected \
	grid-light grid-dark grid-null-wrap grid-error-after-rows grid-capped grid-detail tabs-streaming grid-million \
	query-cancelling query-disconnect-offer conn-reconnect

# Renders SCENE headlessly with the software renderer and saves it (plan KTD15).
SCENE ?= hello
screenshot: all
	$(if $(filter $(SCENE),$(PG_SCENES)),sh pgtest.sh) env SDL_VIDEO_DRIVER=offscreen \
		$(BUILD)/explorer --scene $(SCENE) --screenshot $(BUILD)/$(SCENE).png
	@echo $(BUILD)/$(SCENE).png

# Every scene, into build/explorer/scenes/.
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
PQ18_BUILD = mkdir -p $(BUILD) && $(CC) -std=c11 -g -O1 -Wall -Wextra -I$(ROOT)/include -I$(ROOT)/adapters \
	$(PQ18_CFLAGS) $(PQ_TEST) $(LINK_SRCS) $(LINK_ADAPTERS) $(ROOT)/stdlib/*.c $(PQ18_LIBS) -pthread

$(BUILD)/pq_asan: $(PQ_DEPS)
	$(PQ_BUILD) -fsanitize=address,undefined -fno-sanitize-recover=all -o $@

$(BUILD)/pq_tsan: $(PQ_DEPS)
	$(PQ_BUILD) -fsanitize=thread -o $@

pq-test-asan: $(BUILD)/pq_asan
	sh pgtest.sh $(BUILD)/pq_asan

pq-test-tsan: $(BUILD)/pq_tsan
	TSAN_OPTIONS=halt_on_error=1 sh pgtest.sh $(BUILD)/pq_tsan

pq-test: pq-test-asan pq-test-tsan

# The same test against libpq 18 (KTD20), on the same server.
$(BUILD)/pq18_asan: $(PQ_DEPS) $(PQ18_PC)
	$(PQ18_BUILD) -fsanitize=address,undefined -fno-sanitize-recover=all -o $@

$(BUILD)/pq18_tsan: $(PQ_DEPS) $(PQ18_PC)
	$(PQ18_BUILD) -fsanitize=thread -o $@

pq-test-18-asan: $(BUILD)/pq18_asan
	sh pgtest.sh $(BUILD)/pq18_asan

pq-test-18-tsan: $(BUILD)/pq18_tsan
	TSAN_OPTIONS=halt_on_error=1 sh pgtest.sh $(BUILD)/pq18_tsan

pq-test-18: pq-test-18-asan pq-test-18-tsan

clean:
	rm -rf $(BUILD)
