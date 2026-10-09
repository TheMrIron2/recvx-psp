.DEFAULT_GOAL := psp
.DELETE_ON_ERROR:

PSP_CONFIG ?= psp-config
PSPSDK ?= $(shell $(PSP_CONFIG) --pspsdk-path 2>/dev/null)
ifeq ($(strip $(PSPSDK)),)
$(error PSPSDK not found. Put the PSP toolchain's bin directory on PATH)
endif

BUILD_DIR := build/psp
TARGET := $(BUILD_DIR)/recvx
BUILD_PRX := 1
PSP_FW_VERSION := 600
PSP_LARGE_MEMORY := 0

# Keep the source list explicit as systems are ported to PSP.
PORT_SOURCES := src/psp/expand.c src/psp/selftest.c src/psp/room.c src/psp/room_check.c src/psp/scene.c src/psp/mesh.c
GAME_SOURCES := src/ps2/veronica/prog/flag.c src/ps2/veronica/prog/ps2_NaMem.c
SOURCES := src/psp/main.c src/psp/renderer.c $(PORT_SOURCES) $(GAME_SOURCES)
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJS:.o=.d)
INCDIR := include include/ps2/veronica/prog
CFLAGS := -std=gnu11 -O2 -g -G0 -Wall -Wextra -MMD -MP
LDFLAGS := -Wl,-Map,$(TARGET).map
LIBS := -lpspgum -lpspgu -lm

PSP_EBOOT := $(BUILD_DIR)/EBOOT.PBP
PSP_EBOOT_SFO := $(BUILD_DIR)/PARAM.SFO
PSP_EBOOT_TITLE := Resident Evil: Code Veronica X
PSP_EBOOT_ICON := src/psp/EBOOT/ICON0.png
PSP_EBOOT_PIC1 := src/psp/EBOOT/PIC0.png
PSP_EBOOT_SND0 := src/psp/EBOOT/SND0.at3
EXTRA_TARGETS := $(PSP_EBOOT)
EXTRA_CLEAN := $(DEPS) $(TARGET).map

.PHONY: psp bootstrap
psp bootstrap: all

$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

$(PSP_EBOOT_SFO): Makefile | $(BUILD_DIR)
$(PSP_EBOOT): $(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1) Makefile

include $(PSPSDK)/lib/build.mak

-include $(DEPS)

# Exercise the same portable code on the host, including sanitizers.
HOST_CC ?= cc
HOST_TEST := build/host/port-test
HOST_SHARED := build/host/librecvx-test.so
HOST_FLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude -Iinclude/ps2/veronica/prog
HOST_HEADERS := include/psp/expand.h include/psp/selftest.h include/psp/room.h include/psp/scene.h include/psp/mesh.h \
	include/ps2/veronica/prog/expand.h include/ps2/veronica/prog/flag.h \
	include/ps2/veronica/prog/ps2_NaMem.h include/ps2/veronica/prog/override_katana.h

.PHONY: test
test: $(HOST_TEST) $(HOST_SHARED)
# LeakSanitizer cannot run under this environment's process tracing. The
# address/undefined-behavior checks still run, and test allocations are freed.
	ASAN_OPTIONS=detect_leaks=0 $(HOST_TEST)
	python3 tests/test_expand.py $(HOST_SHARED)
	python3 tests/test_room.py $(HOST_SHARED)
	python3 tests/test_scene.py $(HOST_SHARED)
	python3 tests/test_mesh.py $(HOST_SHARED)

$(HOST_TEST): tests/port_test.c $(PORT_SOURCES) $(GAME_SOURCES) $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/port_test.c $(PORT_SOURCES) $(GAME_SOURCES) -lm -o $@

$(HOST_SHARED): $(PORT_SOURCES) $(GAME_SOURCES) $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fPIC -shared $(PORT_SOURCES) $(GAME_SOURCES) -lm -o $@

ISO ?= recvx.iso
ROOM ?= RM_0000.RDX
.PHONY: assets test-assets preview
assets: $(HOST_SHARED)
	python3 tools/prepare_psp_assets.py --iso "$(ISO)" --room "$(ROOM)" \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/data

test-assets: $(HOST_SHARED) $(HOST_TEST)
	python3 tools/prepare_psp_assets.py --iso "$(ISO)" --room "$(ROOM)" \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/data --host-cc "$(HOST_CC)" --validate-all
	ASAN_OPTIONS=detect_leaks=0 $(HOST_TEST) $(BUILD_DIR)/data

preview: $(HOST_SHARED)
	python3 tools/preview_psp_scene.py --data $(BUILD_DIR)/data \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/room-preview
