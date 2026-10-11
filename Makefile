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
PORT_SOURCES := src/psp/expand.c src/psp/selftest.c src/psp/ninja_math.c src/psp/timing.c src/psp/input.c src/psp/game_tasks.c src/psp/services.c src/psp/event_data.c src/psp/file_stdio.c src/psp/room.c src/psp/room_check.c src/psp/scene.c src/psp/mesh.c src/psp/texture.c src/psp/player.c src/psp/player_asset.c src/psp/actors.c
PSP_PLATFORM_SOURCES := src/psp/platform_psp.c
PSP_LEGACY_SOURCES := src/psp/legacy_runtime.c src/psp/legacy_platform.c src/psp/legacy_script.c src/ps2/veronica/prog/system_init.c
GAME_SOURCES := src/ps2/veronica/prog/flag.c src/ps2/veronica/prog/ps2_NaMem.c
SOURCES := src/psp/main.c src/psp/renderer.c $(PORT_SOURCES) $(PSP_PLATFORM_SOURCES) $(GAME_SOURCES) $(PSP_LEGACY_SOURCES)
LEGACY_PORTABLE_SOURCES := src/ps2/veronica/prog/main.c src/ps2/veronica/prog/system_init.c src/ps2/veronica/prog/system.c src/ps2/veronica/prog/game.c src/ps2/veronica/prog/player.c src/ps2/veronica/prog/Motion.c src/ps2/veronica/prog/camera.c src/ps2/veronica/prog/playpch.c src/ps2/veronica/prog/playpch2.c src/ps2/veronica/prog/pwksub.c src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/hitchkl.c src/ps2/veronica/prog/rutchk.c
LEGACY_PORTABLE_OBJECTS := $(addprefix build/psp/legacy-objects/,$(notdir $(LEGACY_PORTABLE_SOURCES:.c=.o)))
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJS:.o=.d)
INCDIR := include include/ps2/veronica/prog
CFLAGS := -std=gnu11 -O2 -g -G0 -Wall -Wextra -MMD -MP -idirafter include/recvx-decomp-katana/KATANA/Include
LDFLAGS := -Wl,-Map,$(TARGET).map
LIBS := -lpspgum -lpspgu -lpsppower -lm

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
$(PSP_EBOOT): $(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1) $(PSP_EBOOT_SND0) Makefile

include $(PSPSDK)/lib/build.mak

-include $(DEPS)

.PHONY: legacy-portable-check legacy-link-inventory
legacy-portable-check:
	@set -e; for source in $(LEGACY_PORTABLE_SOURCES); do \
		$(CC) $(CPPFLAGS) -D__PSP__ -Iinclude -Iinclude/ps2/veronica/prog \
			-idirafter include/recvx-decomp-katana/KATANA/Include \
			-std=gnu11 -Werror=implicit-function-declaration \
			-fsyntax-only $$source; \
	done

legacy-link-inventory: $(LEGACY_PORTABLE_OBJECTS)
	@for object in $^; do \
		printf '\n%s\n' "$$object"; \
		psp-nm -u "$$object"; \
	done

build/psp/legacy-objects/%.o: src/ps2/veronica/prog/%.c Makefile \
		$(wildcard include/ps2/veronica/prog/*.h) \
		include/psp/legacy_types.h include/psp/platform.h include/psp/services.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -D__PSP__ -Iinclude -Iinclude/ps2/veronica/prog \
		-idirafter include/recvx-decomp-katana/KATANA/Include \
		-std=gnu11 -c $< -o $@

# Exercise the same portable code on the host, including sanitizers.
HOST_CC ?= cc
HOST_TEST := build/host/port-test
HOST_SHARED := build/host/librecvx-test.so
HOST_FLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude -Iinclude/ps2/veronica/prog
HOST_HEADERS := include/psp/abi.h include/psp/ninja_math.h include/psp/timing.h include/psp/input.h include/psp/game_tasks.h include/psp/services.h include/psp/event_data.h include/psp/file.h include/psp/expand.h include/psp/selftest.h include/psp/room.h include/psp/scene.h include/psp/mesh.h include/psp/texture.h include/psp/player.h include/psp/player_asset.h include/psp/actors.h include/psp/legacy_types.h \
	include/ps2/veronica/prog/expand.h include/ps2/veronica/prog/flag.h \
	include/ps2/veronica/prog/ps2_NaMem.h include/ps2/veronica/prog/override_katana.h

.PHONY: test
test: $(HOST_TEST) $(HOST_SHARED)
# LeakSanitizer cannot run under this environment's process tracing. The
# address/undefined-behavior checks still run, and test allocations are freed.
	ASAN_OPTIONS=detect_leaks=0 $(HOST_TEST)
	python3 tests/test_expand.py $(HOST_SHARED)
	python3 tests/test_room.py $(HOST_SHARED)
	python3 tests/test_player.py $(HOST_SHARED)
	python3 tests/test_scene.py $(HOST_SHARED)
	python3 tests/test_mesh.py $(HOST_SHARED)
	python3 tests/test_texture.py $(HOST_SHARED)

$(HOST_TEST): tests/port_test.c $(PORT_SOURCES) $(GAME_SOURCES) $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/port_test.c $(PORT_SOURCES) $(GAME_SOURCES) -lm -o $@

$(HOST_SHARED): $(PORT_SOURCES) $(GAME_SOURCES) $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fPIC -shared $(PORT_SOURCES) $(GAME_SOURCES) -lm -o $@

ISO ?= recvx.iso
ROOM ?=
ROOMS ?= RM_0000.RDX,RM_0010.RDX,RM_0020.RDX,RM_0030.RDX
ifneq ($(strip $(ROOM)),)
ASSET_SELECTION := --room "$(ROOM)"
else
ASSET_SELECTION := --rooms "$(ROOMS)"
endif
.PHONY: assets player-assets route-audit test-assets preview
assets: $(HOST_SHARED)
	python3 tools/prepare_psp_assets.py --iso "$(ISO)" $(ASSET_SELECTION) \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/data

player-assets:
	python3 tools/extract_psp_player_assets.py --iso "$(ISO)" \
		--output $(BUILD_DIR)/data/player-assets

route-audit: $(HOST_SHARED)
	python3 tools/inspect_psp_start_route.py --iso "$(ISO)" \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/start-route.json

test-assets: $(HOST_SHARED) $(HOST_TEST)
	python3 tools/prepare_psp_assets.py --iso "$(ISO)" $(ASSET_SELECTION) \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/data --host-cc "$(HOST_CC)" --validate-all
	ASAN_OPTIONS=detect_leaks=0 $(HOST_TEST) $(BUILD_DIR)/data

preview: $(HOST_SHARED)
	python3 tools/preview_psp_scene.py --data $(BUILD_DIR)/data \
		--library $(HOST_SHARED) --output $(BUILD_DIR)/room-preview $(if $(ROOM),--room "$(ROOM)")
