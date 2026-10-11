.DEFAULT_GOAL := psp
.DELETE_ON_ERROR:

PSP_CONFIG ?= psp-config
PSPSDK ?= $(shell $(PSP_CONFIG) --pspsdk-path 2>/dev/null)
ifeq ($(strip $(PSPSDK)),)
$(error PSPSDK not found. Put the PSP toolchain's bin directory on PATH)
endif

BUILD_PROFILE ?= viewer
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
LEGACY_PORTABLE_SOURCES := src/ps2/veronica/prog/main.c src/ps2/veronica/prog/system_init.c src/ps2/veronica/prog/system.c src/ps2/veronica/prog/pad_task.c src/ps2/veronica/prog/game.c src/ps2/veronica/prog/player.c src/ps2/veronica/prog/dread.c src/ps2/veronica/prog/room.c src/ps2/veronica/prog/subpl.c src/ps2/veronica/prog/eneset.c src/ps2/veronica/prog/cut.c src/ps2/veronica/prog/binfunc.c src/ps2/veronica/prog/Motion.c src/ps2/veronica/prog/camera.c src/ps2/veronica/prog/playpch.c src/ps2/veronica/prog/playpch2.c src/ps2/veronica/prog/pwksub.c src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/hitchkl.c src/ps2/veronica/prog/rutchk.c
LEGACY_PORTABLE_OBJECTS := $(addprefix build/psp/legacy-objects/,$(notdir $(LEGACY_PORTABLE_SOURCES:.c=.o)))
INCDIR := include include/ps2/veronica/prog
CFLAGS := -std=gnu11 -O2 -g -G0 -Wall -Wextra -MMD -MP -idirafter include/recvx-decomp-katana/KATANA/Include
LDFLAGS = -Wl,-Map,$(TARGET).map
LIBS := -lpspgum -lpspgu -lpsppower -lm

EVENT_TEST_SHARED_SOURCES := src/psp/event_core.c src/psp/engine_event_runtime.c src/psp/engine_event_tests.c src/psp/event_data.c src/psp/sha256.c src/psp/expand.c src/psp/room.c src/psp/room_check.c src/psp/file_stdio.c
ROOM_OWNER_SOURCES := src/psp/engine_room_owner.c
ENGINE_TASK_SOURCES := src/psp/live_task_dispatch.c src/psp/engine_pacer.c src/psp/original_clock.c
ifeq ($(BUILD_PROFILE),engine-event-test)
BUILD_DIR := build/psp-engine-event-test
TARGET := $(BUILD_DIR)/recvx-event-test
CFLAGS += -fsigned-char -DRECVX_EVENT_CORE_TEST
SOURCES := src/psp/engine_event_test.c $(EVENT_TEST_SHARED_SOURCES)
else ifeq ($(BUILD_PROFILE),engine-64)
BUILD_DIR := build/psp-engine-64
TARGET := $(BUILD_DIR)/recvx-engine-room-test
PSP_LARGE_MEMORY := 1
CFLAGS += -fsigned-char -DRECVX_EVENT_CORE_TEST
SOURCES := src/psp/engine_room_test.c $(EVENT_TEST_SHARED_SOURCES) $(ROOM_OWNER_SOURCES)
else ifeq ($(BUILD_PROFILE),engine-dispatch-test)
BUILD_DIR := build/psp-engine-dispatch-test
TARGET := $(BUILD_DIR)/recvx-engine-dispatch-test
PSP_LARGE_MEMORY := 1
CFLAGS += -fsigned-char -DRECVX_EVENT_CORE_TEST
SOURCES := src/psp/engine_dispatch_test.c src/psp/renderer.c \
	src/psp/ninja_math.c src/psp/timing.c src/psp/game_tasks.c \
	src/psp/services.c src/psp/scene.c src/psp/mesh.c src/psp/texture.c \
	src/psp/player.c src/psp/player_asset.c src/psp/actors.c \
	$(PSP_PLATFORM_SOURCES) $(EVENT_TEST_SHARED_SOURCES) \
	$(ROOM_OWNER_SOURCES) $(ENGINE_TASK_SOURCES) src/ps2/veronica/prog/flag.c \
	src/ps2/veronica/prog/pad.c src/psp/engine_pad_adapter.c \
	src/ps2/veronica/prog/pad_task.c src/psp/engine_pad_faults.c
else ifeq ($(BUILD_PROFILE),engine-player-test)
BUILD_DIR := build/psp-engine-player-test
TARGET := $(BUILD_DIR)/recvx-engine-player-test
PSP_LARGE_MEMORY := 1
CFLAGS += -fsigned-char -ffunction-sections -fdata-sections -DRECVX_ENGINE_PLAYER_TEST
LDFLAGS += -Wl,--undefined=module_info
SOURCES := src/psp/engine_player_test.c src/psp/renderer.c \
	src/psp/ninja_math.c src/psp/timing.c src/psp/scene.c \
	src/psp/mesh.c src/psp/texture.c src/psp/player_asset.c \
	src/psp/actors.c $(PSP_PLATFORM_SOURCES) \
	src/psp/expand.c src/psp/event_data.c src/psp/file_stdio.c \
	src/psp/room.c src/psp/room_check.c src/psp/event_core.c \
	src/psp/engine_event_runtime.c $(ROOM_OWNER_SOURCES) \
	src/psp/engine_original_fault.c src/psp/engine_original_math.c \
	src/psp/engine_original_mask.c src/psp/engine_original_cut.c \
	src/psp/engine_original_memory.c src/psp/engine_original_model.c \
	src/psp/engine_original_texture.c src/psp/engine_original_object.c \
	src/ps2/veronica/prog/player.c src/ps2/veronica/prog/dread.c \
	src/ps2/veronica/prog/room.c src/ps2/veronica/prog/eneset.c \
	src/ps2/veronica/prog/system.c src/ps2/veronica/prog/camera.c \
	src/ps2/veronica/prog/binfunc.c src/ps2/veronica/prog/Motion.c \
	src/ps2/veronica/prog/MdlPut.c src/ps2/veronica/prog/pwksub.c \
	src/ps2/veronica/prog/playpch.c src/psp/engine_original_effect.c \
	src/psp/engine_original_effect_init.c \
	src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/ps2_NaMem.c \
	src/ps2/veronica/prog/light.c src/psp/engine_original_screen.c \
	src/psp/engine_original_view.c src/ps2/veronica/prog/flag.c
else ifneq ($(BUILD_PROFILE),viewer)
$(error Unknown BUILD_PROFILE '$(BUILD_PROFILE)')
endif

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJS:.o=.d)

ifeq ($(BUILD_PROFILE),engine-player-test)
ENGINE_PLAYER_OBJECTS := $(OBJS)
OBJS := $(BUILD_DIR)/engine-player-core.o
DEPS := $(ENGINE_PLAYER_OBJECTS:.o=.d)
$(OBJS): $(ENGINE_PLAYER_OBJECTS)
	$(LD) -r -Wl,--gc-sections -u main -u module_info -o $@ $^
endif

PSP_EBOOT := $(BUILD_DIR)/EBOOT.PBP
PSP_EBOOT_SFO := $(BUILD_DIR)/PARAM.SFO
PSP_EBOOT_TITLE := Resident Evil: Code Veronica X
ifeq ($(BUILD_PROFILE),engine-event-test)
PSP_EBOOT_TITLE := RECVX Original Event 38 Test
else ifeq ($(BUILD_PROFILE),engine-64)
PSP_EBOOT_TITLE := RECVX Engine Room Test 64MiB
else ifeq ($(BUILD_PROFILE),engine-dispatch-test)
PSP_EBOOT_TITLE := RECVX Original Task Dispatch Test
else ifeq ($(BUILD_PROFILE),engine-player-test)
PSP_EBOOT_TITLE := RECVX Package 4a Original Player Test
endif
PSP_EBOOT_ICON := src/psp/EBOOT/ICON0.png
PSP_EBOOT_PIC1 := src/psp/EBOOT/PIC0.png
PSP_EBOOT_SND0 := src/psp/EBOOT/SND0.at3
EXTRA_TARGETS := $(PSP_EBOOT)
EXTRA_CLEAN := $(DEPS) $(TARGET).map

.PHONY: psp bootstrap engine-event-test engine-event-test-assets engine-64 engine-64-assets engine-dispatch-test engine-dispatch-test-assets engine-player-test engine-player-test-assets
psp bootstrap: all

engine-event-test:
	$(MAKE) BUILD_PROFILE=engine-event-test all

engine-event-test-assets:
	$(MAKE) BUILD_PROFILE=engine-event-test assets ISO="$(ISO)" ROOM=RM_0000.RDX

engine-64:
	$(MAKE) BUILD_PROFILE=engine-64 all

engine-64-assets:
	$(MAKE) BUILD_PROFILE=engine-64 assets ISO="$(ISO)" ROOM= ROOMS="RM_0000.RDX,RM_0010.RDX"

engine-dispatch-test:
	$(MAKE) BUILD_PROFILE=engine-dispatch-test all

engine-dispatch-test-assets:
	$(MAKE) BUILD_PROFILE=engine-dispatch-test assets ISO="$(ISO)" ROOM=RM_0000.RDX

engine-player-test:
	$(MAKE) BUILD_PROFILE=engine-player-test all

engine-player-test-assets:
	$(MAKE) BUILD_PROFILE=engine-player-test assets ISO="$(ISO)" ROOM=RM_0010.RDX
	$(MAKE) BUILD_PROFILE=engine-player-test player-assets ISO="$(ISO)" SYSTEM_EFFECT_ENTRY=1

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
HOST_EVENT_TEST := build/host/event-core-test
HOST_ROOM_OWNER_TEST := build/host/engine-room-owner-test
HOST_TASK_DISPATCH_TEST := build/host/live-task-dispatch-test
HOST_ENGINE_PACER_TEST := build/host/engine-pacer-test
HOST_ORIGINAL_CLOCK_TEST := build/host/original-clock-test
HOST_ENGINE_PAD_TEST := build/host/engine-pad-adapter-test
HOST_ENGINE_OBJECT_TEST := build/host/engine-original-object-test
HOST_FLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude -Iinclude/ps2/veronica/prog
HOST_EVENT_FLAGS := $(HOST_FLAGS) -fsigned-char -D__PSP__ -DRECVX_EVENT_CORE_HOST_TEST -DRECVX_EVENT_CORE_TEST -idirafter include/recvx-decomp-katana/KATANA/Include
HOST_HEADERS := include/psp/abi.h include/psp/ninja_math.h include/psp/timing.h include/psp/input.h include/psp/game_tasks.h include/psp/services.h include/psp/event_data.h include/psp/engine_event_core.h include/psp/engine_event_test.h include/psp/engine_event_runtime.h include/psp/sha256.h include/psp/file.h include/psp/expand.h include/psp/selftest.h include/psp/room.h include/psp/scene.h include/psp/mesh.h include/psp/texture.h include/psp/player.h include/psp/player_asset.h include/psp/actors.h include/psp/legacy_types.h include/psp/live_task_dispatch.h include/psp/engine_pacer.h include/psp/original_clock.h \
	include/ps2/veronica/prog/expand.h include/ps2/veronica/prog/flag.h \
	include/ps2/veronica/prog/ps2_NaMem.h include/ps2/veronica/prog/override_katana.h \
	include/ps2/veronica/prog/event.h include/ps2/veronica/prog/main.h \
	include/ps2/veronica/prog/types.h include/ps2/veronica/prog/event_dispatch_table.inc \
	include/psp/engine_room_owner.h include/psp/engine_pad_adapter.h

.PHONY: test
test: $(HOST_TEST) $(HOST_SHARED) $(HOST_EVENT_TEST) $(HOST_ROOM_OWNER_TEST) $(HOST_TASK_DISPATCH_TEST) $(HOST_ENGINE_PACER_TEST) $(HOST_ORIGINAL_CLOCK_TEST) $(HOST_ENGINE_PAD_TEST) $(HOST_ENGINE_OBJECT_TEST)
# LeakSanitizer cannot run under this environment's process tracing. The
# address/undefined-behavior checks still run, and test allocations are freed.
	ASAN_OPTIONS=detect_leaks=0 $(HOST_TEST)
	python3 tests/test_expand.py $(HOST_SHARED)
	python3 tests/test_room.py $(HOST_SHARED)
	$(HOST_EVENT_TEST) $(if $(wildcard build/psp/data/rooms/RM_0000.meta),build/psp/data)
	$(HOST_TASK_DISPATCH_TEST)
	$(HOST_ENGINE_PACER_TEST)
	$(HOST_ORIGINAL_CLOCK_TEST)
	$(HOST_ENGINE_PAD_TEST)
	$(HOST_ENGINE_OBJECT_TEST)
	@if [ -f build/psp/data/rooms/RM_0010.meta ]; then \
		$(HOST_ROOM_OWNER_TEST) build/psp/data; \
	else \
		echo "SKIP: RM_0010 owner cycles need ISO-derived build/psp/data"; \
	fi
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

$(HOST_EVENT_TEST): tests/event_core_test.c $(EVENT_TEST_SHARED_SOURCES) $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EVENT_FLAGS) tests/event_core_test.c $(EVENT_TEST_SHARED_SOURCES) -o $@

$(HOST_ROOM_OWNER_TEST): tests/engine_room_owner_test.c $(ROOM_OWNER_SOURCES) src/psp/event_data.c src/psp/room.c src/psp/room_check.c src/psp/expand.c src/psp/file_stdio.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) tests/engine_room_owner_test.c $(ROOM_OWNER_SOURCES) \
		src/psp/event_data.c src/psp/room.c src/psp/room_check.c \
		src/psp/expand.c src/psp/file_stdio.c -o $@

$(HOST_TASK_DISPATCH_TEST): tests/live_task_dispatch_test.c src/psp/live_task_dispatch.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) $< src/psp/live_task_dispatch.c -o $@

$(HOST_ENGINE_PACER_TEST): tests/engine_pacer_test.c src/psp/engine_pacer.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) $< src/psp/engine_pacer.c -o $@

$(HOST_ORIGINAL_CLOCK_TEST): tests/original_clock_test.c src/psp/original_clock.c src/ps2/veronica/prog/flag.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EVENT_FLAGS) $< src/psp/original_clock.c src/ps2/veronica/prog/flag.c -o $@

$(HOST_ENGINE_PAD_TEST): tests/engine_pad_adapter_test.c src/psp/engine_pad_adapter.c src/psp/engine_pacer.c src/psp/original_clock.c src/ps2/veronica/prog/flag.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EVENT_FLAGS) $< src/psp/engine_pad_adapter.c \
		src/psp/engine_pacer.c src/psp/original_clock.c \
		src/ps2/veronica/prog/flag.c -o $@

$(HOST_ENGINE_OBJECT_TEST): tests/engine_original_object_test.c src/psp/engine_original_object.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fsigned-char -D__PSP__ \
		-idirafter include/recvx-decomp-katana/KATANA/Include \
		$< src/psp/engine_original_object.c -o $@

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

PLAYER_EFFECT_ARGS = $(if $(SYSTEM_EFFECT_ENTRY),--effect-entry $(SYSTEM_EFFECT_ENTRY),)
player-assets:
	python3 tools/extract_psp_player_assets.py --iso "$(ISO)" \
		--output $(BUILD_DIR)/data/player-assets $(PLAYER_EFFECT_ARGS)

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
