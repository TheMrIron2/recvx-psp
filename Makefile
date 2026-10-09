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
SOURCES := src/psp/main.c src/ps2/veronica/prog/flag.c
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJS:.o=.d)
INCDIR := include/ps2/veronica/prog
CFLAGS := -std=gnu11 -O2 -g -G0 -Wall -Wextra -MMD -MP
LDFLAGS := -Wl,-Map,$(TARGET).map

PSP_EBOOT := $(BUILD_DIR)/EBOOT.PBP
PSP_EBOOT_SFO := $(BUILD_DIR)/PARAM.SFO
PSP_EBOOT_TITLE := Resident Evil: Code Veronica X
PSP_EBOOT_ICON := src/psp/EBOOT/ICON0.png
PSP_EBOOT_PIC1 := src/psp/EBOOT/PIC0.png
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
