.DEFAULT_GOAL := psp
.DELETE_ON_ERROR:

PSP_CONFIG ?= psp-config
PSPSDK ?= $(shell $(PSP_CONFIG) --pspsdk-path 2>/dev/null)
ifeq ($(strip $(PSPSDK)),)
$(error PSPSDK not found. Put the PSP toolchain's bin directory on PATH)
endif

BUILD_PROFILE ?= game
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
LEGACY_PORTABLE_SOURCES := \
	src/ps2/veronica/prog/main.c src/ps2/veronica/prog/system_init.c \
	src/ps2/veronica/prog/system.c src/ps2/veronica/prog/pad_task.c \
	src/ps2/veronica/prog/game.c src/ps2/veronica/prog/player.c \
	src/ps2/veronica/prog/dread.c src/ps2/veronica/prog/room.c \
	src/ps2/veronica/prog/subpl.c src/ps2/veronica/prog/eneset.c \
	src/ps2/veronica/prog/cut.c src/ps2/veronica/prog/event.c \
	src/ps2/veronica/prog/binfunc.c src/ps2/veronica/prog/Motion.c \
	src/ps2/veronica/prog/camera.c src/ps2/veronica/prog/playpch.c \
	src/ps2/veronica/prog/playpch2.c src/ps2/veronica/prog/pwksub.c \
	src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/hitchkl.c \
	src/ps2/veronica/prog/rutchk.c
LEGACY_PORTABLE_OBJECTS := $(addprefix build/psp/legacy-objects/,$(notdir $(LEGACY_PORTABLE_SOURCES:.c=.o)))
INCDIR := include include/ps2/veronica/prog
CFLAGS := -std=gnu11 -O2 -g -G0 -Wall -Wextra -MMD -MP -idirafter include/recvx-decomp-katana/KATANA/Include
LDFLAGS = -Wl,-Map,$(TARGET).map
LIBS := -lpspgum -lpspgu -lpspvfpu -lpspaudio -lpsppower -lm

EVENT_TEST_SHARED_SOURCES := src/psp/event_core.c src/psp/engine_event_runtime.c src/psp/engine_event_tests.c src/psp/event_data.c src/psp/sha256.c src/psp/expand.c src/psp/room.c src/psp/room_check.c src/psp/file_stdio.c
ROOM_OWNER_SOURCES := src/psp/engine_room_owner.c
ENGINE_TASK_SOURCES := src/psp/live_task_dispatch.c src/psp/engine_pacer.c src/psp/original_clock.c
ENGINE_PLAYER_SOURCES := src/psp/engine_player_test.c src/psp/renderer.c \
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
ENGINE_PLAYER_CONTROL_SOURCES := $(filter-out src/psp/engine_player_test.c,$(ENGINE_PLAYER_SOURCES)) \
	src/ps2/veronica/prog/game.c src/ps2/veronica/prog/pad.c \
	src/ps2/veronica/prog/pad_task.c src/psp/engine_pad_adapter.c \
	src/psp/engine_original_clock_psp.c \
	src/psp/engine_pad_faults.c src/psp/live_task_dispatch.c \
	src/ps2/veronica/prog/ps2_NaColi.c \
	src/psp/engine_original_profile_state.c \
	src/psp/engine_original_audio_test.c \
	src/psp/engine_original_lighting_psp.c \
	src/psp/engine_original_event_test.c \
	src/psp/engine_pacer.c src/psp/original_clock.c \
	src/psp/engine_test_input.c
ifeq ($(BUILD_PROFILE),game)
PSP_LARGE_MEMORY := 1
CFLAGS += -fsigned-char -ffunction-sections -fdata-sections -DRECVX_GAME_BOOT -DRECVX_ENGINE_SHARED_EVENT_CORE
LDFLAGS += -Wl,--undefined=module_info
BOOT_UNPORTED_SOURCE := $(BUILD_DIR)/game_boot_unported.S
BOOT_UNPORTED_OBJECT := $(BUILD_DIR)/game_boot_unported.o
BOOT_TABLES_SOURCE := $(BUILD_DIR)/game_boot_tables.c
BOOT_TABLES_OBJECT := $(BUILD_DIR)/game_boot_tables.o
BOOT_INITIALIZERS_SOURCE := $(BUILD_DIR)/game_boot_initializers.c
BOOT_INITIALIZERS_OBJECT := $(BUILD_DIR)/game_boot_initializers.o
BOOT_SEQUENCE_SOURCE := $(BUILD_DIR)/game_boot_sequence.c
BOOT_SEQUENCE_OBJECT := $(BUILD_DIR)/game_boot_sequence.o
BOOT_PLAYER_CONTROL_SOURCE := $(BUILD_DIR)/game_boot_player_control.c
BOOT_PLAYER_CONTROL_OBJECT := $(BUILD_DIR)/game_boot_player_control.o
BOOT_CUT_CONTROL_SOURCE := $(BUILD_DIR)/game_boot_cut_control.c
BOOT_CUT_CONTROL_OBJECT := $(BUILD_DIR)/game_boot_cut_control.o
BOOT_PLAYER_PITCH_SOURCE := $(BUILD_DIR)/game_boot_player_pitch_original.c
BOOT_PLAYER_PITCH_OBJECT := $(BUILD_DIR)/game_boot_player_pitch_original.o
BOOT_PLAYER_COLLISION_SOURCE := $(BUILD_DIR)/game_boot_player_collision_original.c
BOOT_PLAYER_COLLISION_OBJECT := $(BUILD_DIR)/game_boot_player_collision_original.o
BOOT_WALL_SOURCE := $(BUILD_DIR)/game_boot_wall_original.c
BOOT_WALL_OBJECT := $(BUILD_DIR)/game_boot_wall_original.o
BOOT_WALL_GEOMETRY_SOURCE := $(BUILD_DIR)/game_boot_wall_geometry_original.c
BOOT_WALL_GEOMETRY_OBJECT := $(BUILD_DIR)/game_boot_wall_geometry_original.o
BOOT_PLAYER_HEAD_SOURCE := $(BUILD_DIR)/game_boot_player_head_original.c
BOOT_PLAYER_HEAD_OBJECT := $(BUILD_DIR)/game_boot_player_head_original.o
BOOT_WORLD_CONTROL_SOURCE := $(BUILD_DIR)/game_boot_world_control_original.c
BOOT_WORLD_CONTROL_OBJECT := $(BUILD_DIR)/game_boot_world_control_original.o
BOOT_VISIBILITY_SOURCE := $(BUILD_DIR)/game_boot_visibility_original.c
BOOT_VISIBILITY_OBJECT := $(BUILD_DIR)/game_boot_visibility_original.o
BOOT_LIGHT_SOURCE := $(BUILD_DIR)/game_boot_light_original.c
BOOT_LIGHT_OBJECT := $(BUILD_DIR)/game_boot_light_original.o
BOOT_OVERLAY_SOURCE := $(BUILD_DIR)/game_boot_overlay_original.c
BOOT_OVERLAY_OBJECT := $(BUILD_DIR)/game_boot_overlay_original.o
BOOT_PLAYER_ACTION_SOURCE := $(BUILD_DIR)/game_boot_player_action_original.c
BOOT_PLAYER_ACTION_OBJECT := $(BUILD_DIR)/game_boot_player_action_original.o
# Newlib reads these from its CRT, linked after our intermediate GC pass.
BOOT_RUNTIME_ROOTS := -u sce_newlib_attribute -u sce_newlib_heap_kb_size -u sce_newlib_heap_threshold_kb_size
SOURCES := src/psp/game_main.c src/psp/game_boot.c \
	src/psp/game_boot_files.c src/psp/game_boot_services.c src/psp/game_boot_sound.c src/psp/game_boot_audio.c src/psp/game_boot_audio_psp.c \
	src/psp/game_boot_draw.c src/psp/game_boot_texture.c src/psp/game_boot_gu.c src/psp/game_boot_cut.c src/psp/game_boot_checkpoint.c src/psp/engine_original_view.c src/psp/engine_original_lighting_psp.c \
	src/psp/engine_original_memory.c src/psp/engine_original_fault.c \
	src/psp/live_task_dispatch.c src/psp/original_clock.c $(PSP_PLATFORM_SOURCES) \
	src/ps2/veronica/prog/main.c src/ps2/veronica/prog/system_init.c \
	src/ps2/veronica/prog/system.c src/ps2/veronica/prog/adv.c \
	src/ps2/veronica/prog/padman.c src/ps2/veronica/prog/pad.c \
	src/ps2/veronica/prog/pad_task.c src/ps2/veronica/prog/flag.c \
	$(BOOT_UNPORTED_OBJECT) $(BOOT_TABLES_OBJECT) src/psp/services.c \
	src/psp/engine_original_clock_psp.c src/psp/engine_original_math.c \
	src/psp/engine_original_object.c src/psp/game_boot_effect.c \
	src/psp/game_boot_player.c src/psp/game_boot_player_pitch.c src/psp/game_boot_wall.c src/psp/game_boot_wall_geometry.c src/psp/game_boot_player_head.c src/psp/game_boot_world_control.c src/psp/game_boot_visibility.c src/psp/game_boot_light.c src/psp/game_boot_overlay.c src/psp/game_boot_player_action.c src/psp/game_boot_player_collision.c src/psp/player_asset.c src/ps2/veronica/prog/ps2_NaMem.c \
	src/psp/game_boot_room.c src/psp/expand.c src/psp/room.c src/psp/event_data.c \
	src/psp/engine_original_effect.c src/psp/game_boot_mask.c src/psp/game_boot_mask_anim.c src/psp/game_boot_event.c src/psp/game_boot_effect_control.c src/psp/event_core.c src/ps2/veronica/prog/Motion.c \
	src/psp/engine_original_effect_init.c src/ps2/veronica/prog/binfunc.c \
	src/ps2/veronica/prog/camera.c src/psp/engine_original_matrix_cpu.c \
	$(BOOT_PLAYER_HEAD_OBJECT) $(BOOT_WORLD_CONTROL_OBJECT) $(BOOT_VISIBILITY_OBJECT) $(BOOT_LIGHT_OBJECT) $(BOOT_OVERLAY_OBJECT) $(BOOT_PLAYER_ACTION_OBJECT) $(BOOT_INITIALIZERS_OBJECT) $(BOOT_SEQUENCE_OBJECT) $(BOOT_PLAYER_CONTROL_OBJECT) $(BOOT_CUT_CONTROL_OBJECT) $(BOOT_PLAYER_PITCH_OBJECT) $(BOOT_PLAYER_COLLISION_OBJECT) $(BOOT_WALL_OBJECT) $(BOOT_WALL_GEOMETRY_OBJECT)
else ifeq ($(BUILD_PROFILE),engine-event-test)
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
	$(filter-out src/psp/engine_player_test.c src/psp/renderer.c,$(ENGINE_PLAYER_SOURCES))
else ifeq ($(BUILD_PROFILE),engine-player-matrix-test)
BUILD_DIR := build/psp-engine-player-matrix-test
TARGET := $(BUILD_DIR)/recvx-engine-player-matrix-test
PSP_LARGE_MEMORY := 1
CFLAGS += -fsigned-char -ffunction-sections -fdata-sections \
	-DRECVX_ENGINE_PLAYER_TEST -DRECVX_ENGINE_ORIGINAL_MATRIX_CPU \
	-DRECVX_ENGINE_ORIGINAL_CAMERA_TEST
ENGINE_CAMERA_SERVICES_SOURCE := $(BUILD_DIR)/engine_original_camera_services.c
ENGINE_CAMERA_SERVICES_OBJECT := $(BUILD_DIR)/engine_original_camera_services.o
SOURCES := $(filter-out src/psp/engine_original_cut.c,$(ENGINE_PLAYER_SOURCES)) \
	src/ps2/veronica/prog/cut.c src/ps2/veronica/prog/ps2_event.c \
	$(ENGINE_CAMERA_SERVICES_OBJECT) src/psp/engine_original_matrix_cpu.c
else ifeq ($(BUILD_PROFILE),engine-player-control-test)
BUILD_DIR := build/psp-engine-player-control-test
TARGET := $(BUILD_DIR)/recvx-engine-player-control-test
PSP_LARGE_MEMORY := 1
CFLAGS += -fsigned-char -ffunction-sections -fdata-sections \
	-DRECVX_ENGINE_PLAYER_TEST -DRECVX_ENGINE_ORIGINAL_MATRIX_CPU \
	-DRECVX_ENGINE_ORIGINAL_CAMERA_TEST -DRECVX_ENGINE_GAME_CALLBACK_TEST \
	-DRECVX_ENGINE_SHARED_EVENT_CORE -DRECVX_EVENT_ENGINE_TEST \
	-DRECVX_ENGINE_ACTOR_EVENT_TEST \
	-DRECVX_ENGINE_PASSIVE_RENDER -DRECVX_ENGINE_AUTOMATED_INPUT
LDFLAGS += -Wl,--undefined=module_info
ENGINE_CAMERA_SERVICES_SOURCE := $(BUILD_DIR)/engine_original_camera_services.c
ENGINE_CAMERA_SERVICES_OBJECT := $(BUILD_DIR)/engine_original_camera_services.o
ENGINE_OBJECT_RUNTIME_SOURCE := $(BUILD_DIR)/engine_original_object_runtime.c
ENGINE_OBJECT_RUNTIME_OBJECT := $(BUILD_DIR)/engine_original_object_runtime.o
ENGINE_EFFECT_RUNTIME_SOURCE := $(BUILD_DIR)/engine_original_effect_runtime.c
ENGINE_EFFECT_RUNTIME_OBJECT := $(BUILD_DIR)/engine_original_effect_runtime.o
ENGINE_COLLISION_RUNTIME_SOURCE := $(BUILD_DIR)/engine_original_collision_runtime.c
ENGINE_COLLISION_RUNTIME_OBJECT := $(BUILD_DIR)/engine_original_collision_runtime.o
ENGINE_MESSAGE_RUNTIME_SOURCE := $(BUILD_DIR)/engine_original_message_runtime.c
ENGINE_MESSAGE_RUNTIME_OBJECT := $(BUILD_DIR)/engine_original_message_runtime.o
ENGINE_EVENT_COMMANDS_SOURCE := $(BUILD_DIR)/engine_original_event_commands.c
ENGINE_EVENT_COMMANDS_OBJECT := $(BUILD_DIR)/engine_original_event_commands.o
ENGINE_ACTOR_DAMAGE_SOURCE := $(BUILD_DIR)/engine_original_actor_damage.c
ENGINE_ACTOR_DAMAGE_OBJECT := $(BUILD_DIR)/engine_original_actor_damage.o
SOURCES := src/psp/engine_player_test.c \
	$(filter-out src/psp/engine_original_cut.c,$(ENGINE_PLAYER_CONTROL_SOURCES)) \
	src/ps2/veronica/prog/cut.c src/ps2/veronica/prog/ps2_event.c \
	src/ps2/veronica/prog/event.c src/ps2/veronica/prog/subpl.c \
	$(ENGINE_CAMERA_SERVICES_OBJECT) $(ENGINE_ACTOR_DAMAGE_OBJECT) \
	$(ENGINE_OBJECT_RUNTIME_OBJECT) \
	$(ENGINE_EFFECT_RUNTIME_OBJECT) $(ENGINE_COLLISION_RUNTIME_OBJECT) \
	$(ENGINE_MESSAGE_RUNTIME_OBJECT) $(ENGINE_EVENT_COMMANDS_OBJECT) \
	src/psp/engine_original_matrix_cpu.c
else ifeq ($(BUILD_PROFILE),viewer)
BUILD_DIR := build/psp-viewer
TARGET := $(BUILD_DIR)/recvx-viewer
else
$(error Unknown BUILD_PROFILE '$(BUILD_PROFILE)')
endif

OBJS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES)))
DEPS := $(OBJS:.o=.d)

ifneq ($(filter game engine-player-test engine-player-matrix-test engine-player-control-test,$(BUILD_PROFILE)),)
ENGINE_PLAYER_OBJECTS := $(OBJS)
OBJS := $(BUILD_DIR)/$(if $(filter game,$(BUILD_PROFILE)),game-core,engine-player-core).o
DEPS := $(ENGINE_PLAYER_OBJECTS:.o=.d)
$(OBJS): $(ENGINE_PLAYER_OBJECTS)
	$(LD) -r -Wl,--gc-sections -u main -u module_info $(BOOT_RUNTIME_ROOTS) -o $@ $^
endif

# Import descriptors must reach the final PRX link, whose script retains them.
# The intermediate --gc-sections link otherwise removes unreferenced descriptors.
ifeq ($(BUILD_PROFILE),game)
OBJS += $(BUILD_DIR)/src/psp/game_boot_sas.o
DEPS += $(BUILD_DIR)/src/psp/game_boot_sas.d
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
else ifeq ($(BUILD_PROFILE),engine-player-matrix-test)
PSP_EBOOT_TITLE := RECVX Package 4 Original Camera Cut Test
else ifeq ($(BUILD_PROFILE),engine-player-control-test)
PSP_EBOOT_TITLE := RECVX Package 4 Original Game Callback Test
endif
PSP_EBOOT_ICON := src/psp/EBOOT/ICON0.png
PSP_EBOOT_PIC1 := src/psp/EBOOT/PIC0.png
PSP_EBOOT_SND0 := src/psp/EBOOT/SND0.at3
EXTRA_TARGETS := $(PSP_EBOOT)
EXTRA_CLEAN := $(DEPS) $(TARGET).map

.PHONY: psp bootstrap game viewer game-assets engine-event-test engine-event-test-assets engine-64 engine-64-assets engine-dispatch-test engine-dispatch-test-assets engine-player-test engine-player-test-assets engine-player-matrix-test engine-player-matrix-test-assets engine-player-control-test engine-player-control-test-assets
psp bootstrap: all

game:
	$(MAKE) BUILD_PROFILE=game all
	python3 tools/check_game_boot_runtime.py build/psp/recvx.elf

viewer:
	$(MAKE) BUILD_PROFILE=viewer all

game-assets:
	python3 tools/prepare_psp_boot_assets.py --iso "$(ISO)" --output $(BUILD_DIR)/data

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

engine-player-matrix-test:
	$(MAKE) BUILD_PROFILE=engine-player-matrix-test all

engine-player-matrix-test-assets:
	$(MAKE) BUILD_PROFILE=engine-player-matrix-test assets ISO="$(ISO)" ROOM=RM_0010.RDX
	$(MAKE) BUILD_PROFILE=engine-player-matrix-test player-assets ISO="$(ISO)" SYSTEM_EFFECT_ENTRY=1

engine-player-control-test:
	$(MAKE) BUILD_PROFILE=engine-player-control-test all

engine-player-control-test-assets:
	$(MAKE) BUILD_PROFILE=engine-player-control-test assets ISO="$(ISO)" ROOM=RM_0010.RDX
	$(MAKE) BUILD_PROFILE=engine-player-control-test player-assets ISO="$(ISO)" SYSTEM_EFFECT_ENTRY=1

$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ifneq ($(strip $(BOOT_UNPORTED_SOURCE)),)
$(BOOT_UNPORTED_SOURCE): tools/generate_boot_faults.py config/psp_boot_unported.txt
	python3 $< config/psp_boot_unported.txt $@

$(BOOT_UNPORTED_OBJECT): $(BOOT_UNPORTED_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_TABLES_SOURCE): tools/extract_original_boot_tables.py src/ps2/veronica/prog/player.c src/ps2/veronica/prog/message.c src/ps2/veronica/prog/sdfunc.c
	python3 $< $@

$(BOOT_TABLES_OBJECT): $(BOOT_TABLES_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_SEQUENCE_SOURCE): tools/extract_original_game_sequence.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/game.c src/ps2/veronica/prog/screen.c
	python3 $< $@

$(BOOT_SEQUENCE_OBJECT): $(BOOT_SEQUENCE_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_PLAYER_CONTROL_SOURCE): tools/extract_original_player_control.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py $(addprefix src/ps2/veronica/prog/,player.c light.c playpch.c)
	python3 $< $@

$(BOOT_PLAYER_CONTROL_OBJECT): $(BOOT_PLAYER_CONTROL_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_CUT_CONTROL_SOURCE): tools/extract_original_cut_control.py tools/extract_original_boot_initializers.py $(addprefix src/ps2/veronica/prog/,cut.c event.c ps2_event.c)
	python3 $< $@

$(BOOT_CUT_CONTROL_OBJECT): $(BOOT_CUT_CONTROL_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_PLAYER_PITCH_SOURCE): tools/extract_original_player_pitch.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/playpch.c
	python3 $< $@

$(BOOT_PLAYER_PITCH_OBJECT): $(BOOT_PLAYER_PITCH_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_PLAYER_COLLISION_SOURCE): tools/extract_original_player_collision.py src/ps2/veronica/prog/hitchk.c
	python3 $< $@

$(BOOT_PLAYER_COLLISION_OBJECT): $(BOOT_PLAYER_COLLISION_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_WALL_SOURCE): tools/extract_original_wall_control.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/hitchk.c
	python3 $< $@

$(BOOT_WALL_OBJECT): $(BOOT_WALL_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_WALL_GEOMETRY_SOURCE): tools/extract_original_wall_geometry.py src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/ps2_NaColi.c
	python3 $< $@

$(BOOT_WALL_GEOMETRY_OBJECT): $(BOOT_WALL_GEOMETRY_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_PLAYER_HEAD_SOURCE): tools/extract_original_player_head.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py src/ps2/veronica/prog/player.c src/ps2/veronica/prog/pwksub.c
	python3 $< $@

$(BOOT_PLAYER_HEAD_OBJECT): $(BOOT_PLAYER_HEAD_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_WORLD_CONTROL_SOURCE): tools/extract_original_world_control.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py src/ps2/veronica/prog/objitm.c src/ps2/veronica/prog/player.c src/ps2/veronica/prog/weapon.c src/ps2/veronica/prog/MdlPut.c
	python3 $< $@

$(BOOT_WORLD_CONTROL_OBJECT): $(BOOT_WORLD_CONTROL_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_VISIBILITY_SOURCE): tools/extract_original_visibility.py src/ps2/veronica/prog/pwksub.c src/ps2/veronica/prog/ps2_NaColi.c src/ps2/veronica/prog/njplus.c
	python3 $< $@

$(BOOT_VISIBILITY_OBJECT): $(BOOT_VISIBILITY_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_LIGHT_SOURCE): tools/extract_original_light_control.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/light.c
	python3 $< $@

$(BOOT_LIGHT_OBJECT): $(BOOT_LIGHT_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_OVERLAY_SOURCE): tools/extract_original_game_overlay.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/screen.c
	python3 $< $@

$(BOOT_OVERLAY_OBJECT): $(BOOT_OVERLAY_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BOOT_PLAYER_ACTION_SOURCE): tools/extract_original_player_action.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py src/ps2/veronica/prog/player.c src/ps2/veronica/prog/pwksub.c
	python3 $< $@

$(BOOT_PLAYER_ACTION_OBJECT): $(BOOT_PLAYER_ACTION_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

BOOT_INITIALIZER_INPUTS := $(addprefix src/ps2/veronica/prog/,dread.c player.c eneset.c sub1.c message.c njplus.c weapon.c room.c pwksub.c MdlPut.c light.c hitchk.c screen.c face.c face_bh.c event.c subpl.c zonzon1.c effect.c effsub1.c)
$(BOOT_INITIALIZERS_SOURCE): tools/extract_original_boot_initializers.py tools/extract_original_event_commands.py $(BOOT_INITIALIZER_INPUTS)
	python3 $< $@

$(BOOT_INITIALIZERS_OBJECT): $(BOOT_INITIALIZERS_SOURCE) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_CAMERA_SERVICES_SOURCE)),)
$(ENGINE_CAMERA_SERVICES_SOURCE): tools/extract_original_camera_services.py src/ps2/veronica/prog/event.c
	python3 $< src/ps2/veronica/prog/event.c $@

$(ENGINE_CAMERA_SERVICES_OBJECT): $(ENGINE_CAMERA_SERVICES_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_OBJECT_RUNTIME_SOURCE)),)
$(ENGINE_OBJECT_RUNTIME_SOURCE): tools/extract_original_object_runtime.py src/ps2/veronica/prog/objitm.c
	python3 $< src/ps2/veronica/prog/objitm.c $@

$(ENGINE_OBJECT_RUNTIME_OBJECT): $(ENGINE_OBJECT_RUNTIME_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_EFFECT_RUNTIME_SOURCE)),)
$(ENGINE_EFFECT_RUNTIME_SOURCE): tools/extract_original_effect_runtime.py src/ps2/veronica/prog/effect.c src/ps2/veronica/prog/effsub1.c
	python3 $< src/ps2/veronica/prog/effect.c src/ps2/veronica/prog/effsub1.c $@

$(ENGINE_EFFECT_RUNTIME_OBJECT): $(ENGINE_EFFECT_RUNTIME_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_COLLISION_RUNTIME_SOURCE)),)
$(ENGINE_COLLISION_RUNTIME_SOURCE): tools/extract_original_collision_service.py src/ps2/veronica/prog/njplus.c
	python3 $< src/ps2/veronica/prog/njplus.c $@

$(ENGINE_COLLISION_RUNTIME_OBJECT): $(ENGINE_COLLISION_RUNTIME_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_MESSAGE_RUNTIME_SOURCE)),)
$(ENGINE_MESSAGE_RUNTIME_SOURCE): tools/extract_original_message_set.py src/ps2/veronica/prog/message.c
	python3 $< src/ps2/veronica/prog/message.c $@

$(ENGINE_MESSAGE_RUNTIME_OBJECT): $(ENGINE_MESSAGE_RUNTIME_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_EVENT_COMMANDS_SOURCE)),)
$(ENGINE_EVENT_COMMANDS_SOURCE): tools/extract_original_event_commands.py src/ps2/veronica/prog/event.c
	python3 $< src/ps2/veronica/prog/event.c $@

$(ENGINE_EVENT_COMMANDS_OBJECT): $(ENGINE_EVENT_COMMANDS_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

ifneq ($(strip $(ENGINE_ACTOR_DAMAGE_SOURCE)),)
$(ENGINE_ACTOR_DAMAGE_SOURCE): tools/extract_original_actor_damage.py src/ps2/veronica/prog/zonzon1.c
	python3 $< src/ps2/veronica/prog/zonzon1.c $@

$(ENGINE_ACTOR_DAMAGE_OBJECT): $(ENGINE_ACTOR_DAMAGE_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

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
HOST_ENGINE_EVENT_CORE_TEST := build/host/engine-event-core-test
HOST_ROOM_OWNER_TEST := build/host/engine-room-owner-test
HOST_TASK_DISPATCH_TEST := build/host/live-task-dispatch-test
HOST_ENGINE_PACER_TEST := build/host/engine-pacer-test
HOST_ORIGINAL_CLOCK_TEST := build/host/original-clock-test
HOST_ENGINE_PAD_TEST := build/host/engine-pad-adapter-test
HOST_ENGINE_OBJECT_TEST := build/host/engine-original-object-test
HOST_ENGINE_MATRIX_TEST := build/host/engine-original-matrix-test
HOST_ENGINE_CAMERA_TEST := build/host/engine-original-camera-test
HOST_ENGINE_CUT_TEST := build/host/engine-original-cut-test
HOST_ENGINE_COLLISION_TEST := build/host/engine-original-collision-test
HOST_ENGINE_SPLINE_TEST := build/host/engine-original-spline-test
HOST_FLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude -Iinclude/ps2/veronica/prog
HOST_EVENT_FLAGS := $(HOST_FLAGS) -fsigned-char -D__PSP__ -DRECVX_EVENT_CORE_HOST_TEST -DRECVX_EVENT_CORE_TEST -idirafter include/recvx-decomp-katana/KATANA/Include
HOST_ORIGINAL_ENGINE_FLAGS := $(HOST_FLAGS) -fsigned-char -D__PSP__ -idirafter include/recvx-decomp-katana/KATANA/Include
HOST_BOOT_TEST := build/host/game-boot-test
HOST_BOOT_SOURCES := src/psp/game_boot.c src/psp/game_boot_files.c \
	src/psp/game_boot_sound.c src/psp/game_boot_audio.c src/psp/game_boot_audio_psp.c \
	build/host/game_boot_sequence.c \
	build/host/game_boot_wall_original.c \
	build/host/game_boot_wall_geometry_original.c \
	build/host/game_boot_player_head_original.c \
	build/host/game_boot_world_control_original.c \
	build/host/game_boot_visibility_original.c \
	build/host/game_boot_light_original.c \
	build/host/game_boot_overlay_original.c \
	build/host/game_boot_player_action_original.c \
	build/host/game_boot_player_control.c \
	build/host/game_boot_player_pitch_original.c build/host/game_boot_player_collision_original.c \
	build/host/game_boot_cut_control.c src/psp/game_boot_cut.c src/psp/game_boot_checkpoint.c src/psp/engine_original_view.c src/psp/engine_original_lighting_psp.c \
	src/psp/game_boot_services.c src/psp/game_boot_draw.c src/psp/game_boot_texture.c \
	src/psp/engine_original_memory.c src/psp/engine_original_fault.c \
	src/psp/live_task_dispatch.c src/psp/original_clock.c src/psp/services.c \
	src/psp/engine_original_clock_psp.c src/psp/engine_original_math.c \
	src/psp/engine_original_object.c src/psp/game_boot_effect.c \
	src/psp/game_boot_player.c src/psp/game_boot_player_pitch.c src/psp/game_boot_wall.c src/psp/game_boot_wall_geometry.c src/psp/game_boot_player_head.c src/psp/game_boot_world_control.c src/psp/game_boot_visibility.c src/psp/game_boot_light.c src/psp/game_boot_overlay.c src/psp/game_boot_player_action.c src/psp/game_boot_player_collision.c src/psp/player_asset.c src/ps2/veronica/prog/ps2_NaMem.c \
	src/psp/game_boot_room.c src/psp/expand.c src/psp/room.c src/psp/event_data.c \
	src/psp/engine_original_effect.c src/psp/game_boot_mask.c src/psp/game_boot_mask_anim.c src/psp/game_boot_event.c src/psp/game_boot_effect_control.c src/psp/event_core.c src/ps2/veronica/prog/Motion.c \
	src/psp/engine_original_effect_init.c src/ps2/veronica/prog/binfunc.c \
	src/ps2/veronica/prog/camera.c src/psp/engine_original_matrix_cpu.c \
	src/ps2/veronica/prog/main.c src/ps2/veronica/prog/system_init.c \
	src/ps2/veronica/prog/system.c src/ps2/veronica/prog/adv.c \
	src/ps2/veronica/prog/padman.c src/ps2/veronica/prog/pad.c \
	src/ps2/veronica/prog/pad_task.c src/ps2/veronica/prog/flag.c

.PHONY: game-boot-test
game-boot-test: $(HOST_BOOT_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(HOST_BOOT_TEST) build/psp/data
	@mkdir -p build/host/boot-missing/data
	ASAN_OPTIONS=detect_leaks=0 $(HOST_BOOT_TEST) build/host/boot-missing/data --missing-assets

.PHONY: game-boot-audio-test
game-boot-audio-test: build/host/game-boot-audio-test
	ASAN_OPTIONS=detect_leaks=0 $< build/psp/data

build/host/game-boot-audio-test: tests/game_boot_audio_test.c src/psp/game_boot_audio.c src/psp/game_boot_audio_psp.c include/psp/game_boot_audio.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) \
		-isystem $(PSPSDK)/include \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/game_boot_audio_test.c src/psp/game_boot_audio.c src/psp/game_boot_audio_psp.c -lm -o $@

.PHONY: game-boot-vfpu-check
game-boot-vfpu-check: $(BUILD_DIR)/src/psp/game_boot_mask_anim.o
	python3 tools/check_game_boot_vfpu.py $<

.PHONY: game-boot-runtime-check
game-boot-runtime-check: $(TARGET).elf
	python3 tools/check_game_boot_runtime.py $<

.PHONY: game-boot-sequence-test
game-boot-sequence-test: build/host/game-boot-sequence-test
	ASAN_OPTIONS=detect_leaks=0 $<

build/host/game-boot-sequence-test: tests/game_boot_sequence_test.c build/host/game_boot_sequence.c $(wildcard include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error=unused-function \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/game_boot_sequence_test.c build/host/game_boot_sequence.c -lm -o $@

build/host/game_boot_unported.S: tools/generate_boot_faults.py config/psp_boot_unported.txt
	python3 $< config/psp_boot_unported.txt $@ --host

build/host/game_boot_tables.c: tools/extract_original_boot_tables.py src/ps2/veronica/prog/player.c src/ps2/veronica/prog/message.c src/ps2/veronica/prog/sdfunc.c
	python3 $< $@

build/host/game_boot_initializers.c: tools/extract_original_boot_initializers.py tools/extract_original_event_commands.py $(addprefix src/ps2/veronica/prog/,dread.c player.c eneset.c sub1.c message.c njplus.c weapon.c room.c pwksub.c MdlPut.c light.c hitchk.c screen.c face.c face_bh.c event.c subpl.c zonzon1.c effect.c effsub1.c)
	python3 $< $@

build/host/game_boot_sequence.c: tools/extract_original_game_sequence.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/game.c src/ps2/veronica/prog/screen.c
	python3 $< $@

build/host/game_boot_player_control.c: tools/extract_original_player_control.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py $(addprefix src/ps2/veronica/prog/,player.c light.c playpch.c)
	python3 $< $@

build/host/game_boot_cut_control.c: tools/extract_original_cut_control.py tools/extract_original_boot_initializers.py $(addprefix src/ps2/veronica/prog/,cut.c event.c ps2_event.c)
	python3 $< $@

build/host/game_boot_player_pitch_original.c: tools/extract_original_player_pitch.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/playpch.c
	python3 $< $@

build/host/game_boot_player_collision_original.c: tools/extract_original_player_collision.py src/ps2/veronica/prog/hitchk.c
	python3 $< $@

.PHONY: game-boot-cut-test game-boot-player-pitch-test game-boot-player-collision-test
game-boot-cut-test: build/host/game-boot-cut-test
	ASAN_OPTIONS=detect_leaks=0 $<

game-boot-player-pitch-test: build/host/game-boot-player-pitch-test
	ASAN_OPTIONS=detect_leaks=0 $<

game-boot-player-collision-test: build/host/game-boot-player-collision-test
	ASAN_OPTIONS=detect_leaks=0 $<

build/host/game-boot-cut-test: tests/game_boot_cut_test.c build/host/game_boot_cut_control.c src/psp/game_boot_cut.c src/psp/engine_original_math.c src/ps2/veronica/prog/flag.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -DRECVX_GAME_BOOT_HOST_TEST \
		-Wno-error=unused-function -Wno-error=char-subscripts \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game-boot-player-pitch-test: tests/game_boot_player_pitch_test.c src/psp/game_boot_player_pitch.c build/host/game_boot_player_pitch_original.c src/ps2/veronica/prog/Motion.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game-boot-player-collision-test: tests/game_boot_player_collision_test.c src/psp/game_boot_player_collision.c build/host/game_boot_player_collision_original.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error=unused-function -Wno-error=unused-variable \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game_boot_wall_original.c: tools/extract_original_wall_control.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/hitchk.c
	python3 $< $@

build/host/game_boot_wall_geometry_original.c: tools/extract_original_wall_geometry.py src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/ps2_NaColi.c
	python3 $< $@

build/host/game_boot_player_head_original.c: tools/extract_original_player_head.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py src/ps2/veronica/prog/player.c src/ps2/veronica/prog/pwksub.c
	python3 $< $@

build/host/game_boot_world_control_original.c: tools/extract_original_world_control.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py src/ps2/veronica/prog/objitm.c src/ps2/veronica/prog/player.c src/ps2/veronica/prog/weapon.c src/ps2/veronica/prog/MdlPut.c
	python3 $< $@

build/host/game_boot_visibility_original.c: tools/extract_original_visibility.py src/ps2/veronica/prog/pwksub.c src/ps2/veronica/prog/ps2_NaColi.c src/ps2/veronica/prog/njplus.c
	python3 $< $@

build/host/game_boot_light_original.c: tools/extract_original_light_control.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/light.c
	python3 $< $@

build/host/game_boot_overlay_original.c: tools/extract_original_game_overlay.py tools/extract_original_boot_initializers.py src/ps2/veronica/prog/screen.c
	python3 $< $@

build/host/game_boot_player_action_original.c: tools/extract_original_player_action.py tools/extract_original_boot_initializers.py tools/extract_original_boot_tables.py src/ps2/veronica/prog/player.c src/ps2/veronica/prog/pwksub.c
	python3 $< $@

$(HOST_BOOT_TEST): tests/game_boot_test.c $(HOST_BOOT_SOURCES) build/host/game_boot_unported.S build/host/game_boot_tables.c build/host/game_boot_initializers.c $(HOST_HEADERS) include/psp/game_boot.h Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Dasm=__asm__ -DRECVX_GAME_BOOT_HOST_TEST -DRECVX_GAME_BOOT -DRECVX_ENGINE_SHARED_EVENT_CORE -DRECVX_EVENT_CORE_HOST_TEST -Wno-error -fno-pie -no-pie \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-ffunction-sections -fdata-sections $< $(HOST_BOOT_SOURCES) \
		build/host/game_boot_unported.S build/host/game_boot_tables.c build/host/game_boot_initializers.c \
		-Wl,--gc-sections -lm -o $@

$(HOST_BOOT_TEST): $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h)

HOST_BOOT_ABI_TEST := build/host/game-boot-abi-test
HOST_I386_LIBDIR ?= /usr/lib32
HOST_I386_SYS_INCDIR ?= /usr/include/$(shell $(HOST_CC) -dumpmachine)
HOST_I386_LOADER ?= /lib/ld-linux.so.2
.PHONY: game-boot-abi-test
game-boot-abi-test: $(HOST_BOOT_ABI_TEST)
	$(HOST_BOOT_ABI_TEST) build/psp/data

build/host/game_boot_unported_i386.S: tools/generate_boot_faults.py config/psp_boot_unported.txt
	python3 $< config/psp_boot_unported.txt $@ --host32

# These tests use only the installed i386 shared runtime. No optional static
# glibc APIs are needed; leave the missing multilib feature-stub header empty.
build/host/i386-include/gnu/stubs-32.h:
	@mkdir -p $(dir $@)
	@touch $@

$(HOST_BOOT_ABI_TEST): tests/game_boot_test.c tests/game_boot_i386_start.S $(HOST_BOOT_SOURCES) \
		build/host/game_boot_unported_i386.S build/host/game_boot_tables.c \
		build/host/game_boot_initializers.c build/host/i386-include/gnu/stubs-32.h \
		$(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -m32 -Dasm=__asm__ -DRECVX_GAME_BOOT_HOST_TEST -DRECVX_GAME_BOOT -DRECVX_ENGINE_SHARED_EVENT_CORE -DRECVX_EVENT_CORE_HOST_TEST \
		-Ibuild/host/i386-include -I$(HOST_I386_SYS_INCDIR) -Wno-error -fno-pie -no-pie \
		-ffunction-sections -fdata-sections -nostartfiles -nodefaultlibs \
		tests/game_boot_test.c tests/game_boot_i386_start.S $(HOST_BOOT_SOURCES) \
		build/host/game_boot_unported_i386.S build/host/game_boot_tables.c build/host/game_boot_initializers.c \
		-Wl,--gc-sections -Wl,--dynamic-linker=$(HOST_I386_LOADER) \
		$(HOST_I386_LIBDIR)/libc.so.6 $(HOST_I386_LIBDIR)/libm.so.6 $(HOST_I386_LIBDIR)/libgcc_s.so.1 -o $@
HOST_HEADERS := include/psp/abi.h include/psp/ninja_math.h include/psp/timing.h include/psp/input.h include/psp/game_tasks.h include/psp/services.h include/psp/event_data.h include/psp/engine_event_core.h include/psp/engine_event_test.h include/psp/engine_event_runtime.h include/psp/sha256.h include/psp/file.h include/psp/expand.h include/psp/selftest.h include/psp/room.h include/psp/scene.h include/psp/mesh.h include/psp/texture.h include/psp/player.h include/psp/player_asset.h include/psp/actors.h include/psp/legacy_types.h include/psp/live_task_dispatch.h include/psp/engine_pacer.h include/psp/original_clock.h \
	include/ps2/veronica/prog/expand.h include/ps2/veronica/prog/flag.h \
	include/ps2/veronica/prog/ps2_NaMem.h include/ps2/veronica/prog/override_katana.h \
	include/ps2/veronica/prog/event.h include/ps2/veronica/prog/main.h \
	include/ps2/veronica/prog/types.h include/ps2/veronica/prog/event_dispatch_table.inc \
	include/psp/engine_room_owner.h include/psp/engine_pad_adapter.h \
	include/psp/platform.h include/psp/engine_test_input.h \
	include/psp/engine_original_matrix.h include/psp/engine_original_fault.h \
	include/psp/engine_original_view.h

.PHONY: test
test: $(HOST_TEST) $(HOST_SHARED) $(HOST_EVENT_TEST) $(HOST_ENGINE_EVENT_CORE_TEST) $(HOST_ROOM_OWNER_TEST) $(HOST_TASK_DISPATCH_TEST) $(HOST_ENGINE_PACER_TEST) $(HOST_ORIGINAL_CLOCK_TEST) $(HOST_ENGINE_PAD_TEST) $(HOST_ENGINE_OBJECT_TEST) $(HOST_ENGINE_MATRIX_TEST) $(HOST_ENGINE_CAMERA_TEST) $(HOST_ENGINE_CUT_TEST) $(HOST_ENGINE_COLLISION_TEST) $(HOST_ENGINE_SPLINE_TEST)
# LeakSanitizer cannot run under this environment's process tracing. The
# address/undefined-behavior checks still run, and test allocations are freed.
	ASAN_OPTIONS=detect_leaks=0 $(HOST_TEST)
	python3 tests/test_expand.py $(HOST_SHARED)
	python3 tests/test_room.py $(HOST_SHARED)
	$(HOST_EVENT_TEST) $(if $(wildcard build/psp/data/rooms/RM_0000.meta),build/psp/data)
	$(HOST_ENGINE_EVENT_CORE_TEST)
	$(HOST_TASK_DISPATCH_TEST)
	$(HOST_ENGINE_PACER_TEST)
	$(HOST_ORIGINAL_CLOCK_TEST)
	$(HOST_ENGINE_PAD_TEST)
	$(HOST_ENGINE_OBJECT_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(HOST_ENGINE_MATRIX_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(HOST_ENGINE_CAMERA_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(HOST_ENGINE_CUT_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(HOST_ENGINE_COLLISION_TEST)
	ASAN_OPTIONS=detect_leaks=0 $(HOST_ENGINE_SPLINE_TEST)
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

$(HOST_ENGINE_EVENT_CORE_TEST): tests/engine_event_core_test.c src/psp/event_core.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fsigned-char -D__PSP__ \
		-DRECVX_EVENT_CORE_HOST_TEST -DRECVX_EVENT_ENGINE_TEST \
		-idirafter include/recvx-decomp-katana/KATANA/Include \
		tests/engine_event_core_test.c src/psp/event_core.c -o $@

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

$(HOST_ENGINE_PAD_TEST): tests/engine_pad_adapter_test.c src/psp/engine_pad_adapter.c src/psp/engine_test_input.c src/psp/engine_pacer.c src/psp/original_clock.c src/ps2/veronica/prog/pad.c src/ps2/veronica/prog/flag.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_EVENT_FLAGS) -Wno-error=sign-compare \
		-Wno-error=discarded-qualifiers -Wno-error=char-subscripts \
		$< src/psp/engine_pad_adapter.c src/psp/engine_test_input.c \
		src/psp/engine_pacer.c src/psp/original_clock.c \
		src/ps2/veronica/prog/pad.c src/ps2/veronica/prog/flag.c -o $@

$(HOST_ENGINE_OBJECT_TEST): tests/engine_original_object_test.c src/psp/engine_original_object.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -fsigned-char -D__PSP__ \
		-idirafter include/recvx-decomp-katana/KATANA/Include \
		$< src/psp/engine_original_object.c -o $@

$(HOST_ENGINE_MATRIX_TEST): tests/engine_original_matrix_test.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -fsanitize=address,undefined \
		-fno-omit-frame-pointer $< src/psp/engine_original_matrix_cpu.c \
		src/psp/engine_original_math.c src/psp/engine_original_fault.c -lm -o $@

$(HOST_ENGINE_CAMERA_TEST): tests/engine_original_camera_test.c src/ps2/veronica/prog/camera.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_view.c src/psp/engine_original_memory.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -DRECVX_ENGINE_ORIGINAL_MATRIX_CPU \
		-ffunction-sections -fdata-sections \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/engine_original_camera_test.c src/ps2/veronica/prog/camera.c \
		src/psp/engine_original_matrix_cpu.c src/psp/engine_original_view.c \
		src/psp/engine_original_memory.c src/psp/engine_original_math.c \
		src/psp/engine_original_fault.c -Wl,--gc-sections -lm -o $@

$(HOST_ENGINE_CUT_TEST): tests/engine_original_cut_test.c src/ps2/veronica/prog/cut.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error \
		-ffunction-sections -fdata-sections \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/engine_original_cut_test.c src/ps2/veronica/prog/cut.c \
		-Wl,--gc-sections -lm -o $@

$(HOST_ENGINE_COLLISION_TEST): tests/engine_original_collision_test.c src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/ps2_NaColi.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error \
		-ffunction-sections -fdata-sections \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/engine_original_collision_test.c \
		src/ps2/veronica/prog/hitchk.c src/ps2/veronica/prog/ps2_NaColi.c \
		src/psp/engine_original_math.c src/psp/engine_original_fault.c \
		-Wl,--gc-sections -lm -o $@

$(HOST_ENGINE_SPLINE_TEST): tests/engine_original_spline_test.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -O1 -g -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer $< \
		src/psp/engine_original_math.c src/psp/engine_original_fault.c \
		-Wl,--gc-sections -lm -o $@

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
ifeq ($(BUILD_PROFILE),game)
	python3 tools/prepare_psp_boot_assets.py --iso "$(ISO)" --output $(BUILD_DIR)/data
endif
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

.PHONY: game-boot-wall-test game-boot-wall-geometry-test
game-boot-wall-test: build/host/game-boot-wall-test
	ASAN_OPTIONS=detect_leaks=0 $<
game-boot-wall-geometry-test: build/host/game-boot-wall-geometry-test
	ASAN_OPTIONS=detect_leaks=0 $<

GAME_BOOT_WALL_GEOMETRY_TEST_SOURCES := src/psp/game_boot_wall_geometry.c build/host/game_boot_wall_geometry_original.c src/psp/engine_original_math.c src/psp/engine_original_fault.c
build/host/game-boot-wall-geometry-test: tests/game_boot_wall_geometry_test.c $(GAME_BOOT_WALL_GEOMETRY_TEST_SOURCES) $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error=unused-function \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game-boot-wall-test: tests/game_boot_wall_test.c src/psp/game_boot_wall.c build/host/game_boot_wall_original.c $(GAME_BOOT_WALL_GEOMETRY_TEST_SOURCES) src/psp/game_boot_player_collision.c build/host/game_boot_player_collision_original.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error=unused-function -Wno-error=unused-variable \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

.PHONY: game-boot-player-head-test
game-boot-player-head-test: build/host/game-boot-player-head-test
	ASAN_OPTIONS=detect_leaks=0 $<

build/host/game-boot-player-head-test: tests/game_boot_player_head_test.c src/psp/game_boot_player_head.c build/host/game_boot_player_head_original.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

.PHONY: game-boot-visibility-test game-boot-world-control-test
game-boot-visibility-test: build/host/game-boot-visibility-test
	ASAN_OPTIONS=detect_leaks=0 $<

game-boot-world-control-test: build/host/game-boot-world-control-test
	ASAN_OPTIONS=detect_leaks=0 $<

build/host/game-boot-visibility-test: tests/game_boot_visibility_test.c src/psp/game_boot_visibility.c build/host/game_boot_visibility_original.c $(GAME_BOOT_WALL_GEOMETRY_TEST_SOURCES) $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game-boot-world-control-test: tests/game_boot_world_control_test.c src/psp/game_boot_world_control.c build/host/game_boot_world_control_original.c src/psp/engine_original_math.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_memory.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -DRECVX_GAME_BOOT_HOST_TEST -DRECVX_WORLD_CONTROL_TEST \
		-Wno-error=unused-function -Wno-error=implicit-fallthrough \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

.PHONY: game-boot-light-test game-boot-overlay-test game-boot-player-action-test
game-boot-light-test: build/host/game-boot-light-test
	ASAN_OPTIONS=detect_leaks=0 $<

game-boot-overlay-test: build/host/game-boot-overlay-test
	ASAN_OPTIONS=detect_leaks=0 $<

game-boot-player-action-test: build/host/game-boot-player-action-test
	ASAN_OPTIONS=detect_leaks=0 $<

build/host/game-boot-light-test: tests/game_boot_light_test.c build/host/game_boot_light_original.c src/psp/game_boot_light.c src/psp/game_boot_world_control.c src/psp/engine_original_lighting_psp.c src/psp/engine_original_view.c src/psp/engine_original_math.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -DRECVX_GAME_BOOT_HOST_TEST -Wno-error=unused-function \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game-boot-overlay-test: tests/game_boot_overlay_test.c src/psp/game_boot_overlay.c build/host/game_boot_overlay_original.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error=unused-function \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@

build/host/game-boot-player-action-test: tests/game_boot_player_action_test.c src/psp/game_boot_player_action.c build/host/game_boot_player_action_original.c src/psp/engine_original_matrix_cpu.c src/psp/engine_original_math.c src/psp/engine_original_fault.c $(wildcard include/psp/*.h include/ps2/veronica/prog/*.h) Makefile
	$(HOST_CC) $(HOST_ORIGINAL_ENGINE_FLAGS) -Wno-error \
		-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
		-ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -lm -o $@
