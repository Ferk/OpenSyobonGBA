# OpenSyobonGBA - native Game Boy Advance build

TARGET      := OpenSyobonGBA
BUILD       := build
SOURCES     := src
INCLUDES    := include
DATA        := gfx
AUDIO       := audio
LEVELS      := levels

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

ifeq ($(wildcard $(strip $(DEVKITPRO))),)
$(error DEVKITPRO not found in $(DEVKITPRO). Please set the correct path in the DEVKITPRO environment variable)
endif

ifeq ($(wildcard $(strip $(DEVKITARM))),)
$(error DEVKITARM not found in $(DEVKITARM). Please set the correct path in the DEVKITARM environment variable)
endif


LIBGBA      := $(DEVKITPRO)/libgba
TOOLS_BIN   := $(DEVKITPRO)/tools/bin
GBAFIX      := $(TOOLS_BIN)/gbafix
PADBIN      := $(TOOLS_BIN)/padbin
GRIT        := $(TOOLS_BIN)/grit
MMUTIL      := $(TOOLS_BIN)/mmutil
BIN2S       := $(TOOLS_BIN)/bin2s
MAXMOD_H    := $(LIBGBA)/include/maxmod.h
MAXMOD_LIB  := $(LIBGBA)/lib/libmm.a

ifeq ($(wildcard $(LIBGBA)/include/gba.h),)
$(error libgba was not found at $(LIBGBA). Install the devkitPro gba-dev package)
endif

include $(DEVKITARM)/gba_rules

ARCH        := -mthumb -mthumb-interwork
SPECS       := -specs=gba.specs

CFLAGS      := -Wall -Wextra -O2 -mcpu=arm7tdmi -mtune=arm7tdmi $(ARCH) \
               -fomit-frame-pointer \
               -I$(INCLUDES) -I$(BUILD) -I$(LIBGBA)/include
ASFLAGS     := $(ARCH)
LDFLAGS     := $(SPECS) $(ARCH) -L$(LIBGBA)/lib \
               -Wl,-Map,$(BUILD)/$(TARGET).map

CC          := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY     := $(DEVKITARM)/bin/arm-none-eabi-objcopy

MAP_SOURCES := $(wildcard $(LEVELS)/*.tmj)
MAP_C       := $(patsubst $(LEVELS)/%.tmj,$(SOURCES)/generated/%_data.c,$(MAP_SOURCES))
MAP_H       := $(patsubst $(LEVELS)/%.tmj,$(INCLUDES)/generated/%_data.h,$(MAP_SOURCES))

CFILES      := $(wildcard $(SOURCES)/*.c) $(MAP_C)
SFILES      := $(wildcard $(SOURCES)/*.s)
PNGFILES    := $(DATA)/player_16.png $(DATA)/tiles_16.png $(DATA)/traps_16.png \
               $(DATA)/items_16.png $(DATA)/enemies_16.png
AUDIOFILES  := $(AUDIO)/block_hit.wav $(AUDIO)/brick_break.wav \
               $(AUDIO)/death.wav $(AUDIO)/jump.wav \
               $(AUDIO)/trap_trigger.wav $(AUDIO)/bgm1-1.xm
GFX_HEADERS := $(patsubst $(DATA)/%.png,$(BUILD)/%.h,$(PNGFILES))

OFILES      := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES)) \
               $(patsubst $(SOURCES)/%.s,$(BUILD)/%.o,$(SFILES)) \
               $(patsubst $(DATA)/%.png,$(BUILD)/%.o,$(PNGFILES))

ifneq ($(strip $(AUDIOFILES)),)
ifneq ($(wildcard $(MMUTIL)),)
ifneq ($(wildcard $(MAXMOD_H)),)
ifneq ($(wildcard $(MAXMOD_LIB)),)
USE_MAXMOD  := 1
endif
endif
endif
endif

ifeq ($(USE_MAXMOD),1)
CFLAGS      += -DUSE_MAXMOD
LIBS        := -lmm -lgba
OFILES      += $(BUILD)/soundbank.o
SOUND_HEADERS := $(BUILD)/soundbank.h
SOUND_STAMP := $(BUILD)/soundbank.stamp
else
LIBS        := -lgba
SOUND_HEADERS :=
SOUND_STAMP :=
ifneq ($(strip $(AUDIOFILES)),)
$(warning Maxmod audio assets found, but mmutil/maxmod.h/libmm.a is missing; audio will compile as stubs)
endif
endif

ifeq ($(DEBUG_STAGE_SELECT),1)
CFLAGS      += -DDEBUG_STAGE_SELECT
endif

DEPS        := $(OFILES:.o=.d)

.PHONY: all clean assets dirs prepare-assets prepare-audio

all: dirs $(TARGET).gba

dirs:
	@mkdir -p $(BUILD) $(BUILD)/generated $(SOURCES)/generated $(INCLUDES)/generated

assets: $(patsubst $(DATA)/%.png,$(BUILD)/%.c,$(PNGFILES))

prepare-assets:
	python3 tools/build_assets.py

prepare-audio:
	python3 tools/build_audio.py

$(TARGET).gba: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(wildcard $(GBAFIX)),)
	$(GBAFIX) $@
else ifneq ($(wildcard $(PADBIN)),)
	$(PADBIN) 256 $@
	@echo "warning: gbafix not found; install devkitPro gba-tools/general-tools for a fully fixed ROM header"
else
	@echo "warning: neither gbafix nor padbin found; ROM header was not post-processed"
endif

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/%.o: $(SOURCES)/%.c | dirs
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/level.o $(BUILD)/player.o $(BUILD)/traps.o $(BUILD)/enemy.o: $(GFX_HEADERS)
$(BUILD)/level.o $(BUILD)/traps.o: $(MAP_H)
$(BUILD)/audio.o: $(SOUND_HEADERS)

$(SOURCES)/generated/%_data.c $(INCLUDES)/generated/%_data.h &: $(LEVELS)/%.tmj tools/compile_map.py | dirs
	python3 tools/compile_map.py $< --source $(SOURCES)/generated/$*_data.c --header $(INCLUDES)/generated/$*_data.h --symbol-prefix $*

$(BUILD)/%.o: $(SOURCES)/%.s | dirs
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.c $(BUILD)/%.h: $(DATA)/%.png | dirs
	$(GRIT) $< -ftc -fh -gB4 -gTFF00FF -m! -p -o$(BUILD)/$*

ifeq ($(USE_MAXMOD),1)
$(SOUND_STAMP): $(AUDIOFILES) | dirs
	$(MMUTIL) $^ -o$(BUILD)/soundbank.bin -h$(BUILD)/soundbank.h
	@touch $@

$(BUILD)/soundbank.bin $(BUILD)/soundbank.h: $(SOUND_STAMP)

$(BUILD)/soundbank.s: $(BUILD)/soundbank.bin | dirs
	$(BIN2S) $< > $@

$(BUILD)/soundbank.o: $(BUILD)/soundbank.s | dirs
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@
endif

$(BUILD)/%.o: $(BUILD)/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	@rm -rf $(BUILD) $(TARGET).gba

-include $(DEPS)
