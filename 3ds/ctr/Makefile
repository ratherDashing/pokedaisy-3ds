# PokeDaisy for the New 3DS: the companion core (../core), the session (../host), the 3DS host (this folder) and the
# emulator (../emu: armGBA with its ARM recompiler, Gambatte), packed into a .3dsx. `make 3ds` in ../ runs this in the
# devkitpro/devkitarm image (devkitARM with libctru, citro3d, citro2d and zlib).
#
#   make                 pokedaisy.3dsx
#   make VARIANT=dev     pokedaisy-dev.3dsx: the Wi-Fi link (devlink.c), its own SD folder
#   make VARIANT=test    pokedaisy-test.3dsx: a script instead of hands (autotest.h), for the Azahar emulator
#   make cia             pokedaisy.cia as well (makerom and bannertool on the PATH)

ifeq ($(strip $(DEVKITARM)),)
$(error "Set DEVKITARM in the environment: export DEVKITARM=<devkitPro>/devkitARM")
endif
include $(DEVKITARM)/3ds_rules
.DEFAULT_GOAL := all

REPO    := $(abspath ../..)
VARIANT ?= release
ifeq ($(VARIANT),dev)
TARGET  := pokedaisy-dev
TITLE   := PokeDaisy dev
DEFS    := -DPD_DEV
else ifeq ($(VARIANT),test)
TARGET  := pokedaisy-test
TITLE   := PokeDaisy test
DEFS    := -DPD_AUTOTEST
else
TARGET  := pokedaisy
TITLE   := PokeDaisy
DEFS    :=
endif
BUILD   := build-$(VARIANT)

ARMGBA := $(REPO)/third_party/armgba
ARMGBA_BACKEND := a32
include $(REPO)/3ds/emu/armgba.mk

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
# As armGBA-3DS builds (its Makefile says why): -fwrapv or every game hangs after 128 seconds, -flto because the
# core's hot paths cross its files.
COMMON   := -g -fwrapv -flto -fomit-frame-pointer -ffunction-sections -mword-relocations $(ARCH) \
            -D__3DS__ -I$(CTRULIB)/include -I$(PORTLIBS)/include
CFLAGS   := $(COMMON) -std=gnu11
CXXFLAGS := $(COMMON) -std=gnu++11 -fno-exceptions -fno-rtti
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g -O3 -fwrapv -flto=auto -mword-relocations $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
LIBS     := -L$(CTRULIB)/lib -L$(PORTLIBS)/lib -lcitro2d -lcitro3d -lctru -lz -lm

D3  := $(REPO)/3ds
CPP := $(REPO)/app/src/main/cpp
PD_SRC := $(wildcard $(D3)/ctr/*.c) $(wildcard $(D3)/core/*.c) $(wildcard $(D3)/host/*.c) \
	$(REPO)/linux/fly.c $(REPO)/linux/field_items.c $(REPO)/linux/gb_field.c $(REPO)/linux/wild.c $(REPO)/linux/hud.c $(REPO)/linux/quick_menu.c
GLUE_SRC := $(D3)/emu/pd_emu.c $(D3)/emu/pd_emu_call.c
# armGBA's own 3DS host code, as it is: executable memory for the recompiler and its cache flushes, the render and
# sound threads on the New 3DS's other cores, the warm list. host.c answers what it asks a frontend.
ARMGBA_HOST_SRC := $(ARMGBA)/platform/3ds/source/jitmem.c $(ARMGBA)/platform/3ds/source/svc.s

obj = $(patsubst $(REPO)/%,$(BUILD)/%.o,$(1))
PD_OBJ       := $(call obj,$(PD_SRC))
GLUE_OBJ     := $(call obj,$(GLUE_SRC))
HOST_OBJ     := $(call obj,$(ARMGBA_HOST_SRC))
CORE_OBJ     := $(call obj,$(ARMGBA_CORE_SRC))
LIBRETRO_OBJ := $(call obj,$(ARMGBA_LIBRETRO_SRC))
GAMBATTE_OBJ := $(call obj,$(ARMGBA_GAMBATTE_SRC))
ARCHIVE_OBJ  := $(call obj,$(ARMGBA_ARCHIVE_SRC))
# pk_wide (the app's EXPAND; here only its reading of the scene) keeps a dozen buffers the size of its largest window,
# 800 x 800: 12 MiB that a 32 MiB game and the recompiler's code need more. It is built from a copy with that window
# the top screen's size, 400 x 240 - the app's own files stay as they are.
WIDE_OBJ := $(BUILD)/gen/pk_wide.c.o $(BUILD)/gen/pk_wide_gb.c.o
$(BUILD)/gen/pk_wide.h: $(CPP)/pk_wide.h
	@mkdir -p $(dir $@)
	@sed -e 's/^#define PK_WIDE_MAX_VIEW_W 800$$/#define PK_WIDE_MAX_VIEW_W 400/' -e 's/^#define PK_WIDE_MAX_VIEW_H 800$$/#define PK_WIDE_MAX_VIEW_H 240/' $< > $@
	@grep -q 'PK_WIDE_MAX_VIEW_W 400' $@ && grep -q 'PK_WIDE_MAX_VIEW_H 240' $@
$(BUILD)/gen/%.c: $(CPP)/%.c $(BUILD)/gen/pk_wide.h
	@cp $< $@
$(WIDE_OBJ): $(BUILD)/gen/%.c.o: $(BUILD)/gen/%.c
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) $(EXTRA) -c $< -o $@
.SECONDARY: $(BUILD)/gen/pk_wide.c $(BUILD)/gen/pk_wide_gb.c

OBJ := $(PD_OBJ) $(WIDE_OBJ) $(GLUE_OBJ) $(HOST_OBJ) $(CORE_OBJ) $(LIBRETRO_OBJ) $(GAMBATTE_OBJ) $(ARCHIVE_OBJ)

# The seam's headers stand where mGBA's did (emu/include), so the code shared with the mGBA hosts builds unchanged.
PD_INC := -I$(D3)/emu/include -I$(D3)/core -I$(REPO)/linux -I$(CPP)
$(PD_OBJ):       EXTRA := -O2 -Wall -Wextra $(PD_INC) $(DEFS) -DM_CORE_GBA -DM_CORE_GB -DPK_WIDE_NO_GUARD -DPK_WIDE_CACHE
$(WIDE_OBJ):     EXTRA := -O2 -Wall $(PD_INC) -DM_CORE_GBA -DM_CORE_GB -DPK_WIDE_NO_GUARD -DPK_WIDE_CACHE
$(GLUE_OBJ):     EXTRA := -O2 -Wall -Wextra $(PD_INC) $(ARMGBA_GLUE_INC)
$(HOST_OBJ):     EXTRA := -O2 -Wall -I$(ARMGBA)/src -I$(ARMGBA)/libretro $(ARMGBA_ARCHIVE_INC)
$(CORE_OBJ):     EXTRA := $(ARMGBA_CORE_FLAGS)
$(LIBRETRO_OBJ): EXTRA := $(ARMGBA_LIBRETRO_FLAGS)
$(GAMBATTE_OBJ): EXTRA := $(ARMGBA_GAMBATTE_FLAGS)
$(ARCHIVE_OBJ):  EXTRA := $(ARMGBA_ARCHIVE_FLAGS)

.PHONY: all cia clean
all: $(TARGET).3dsx

$(BUILD)/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) $(EXTRA) -c $< -o $@

$(BUILD)/%.cpp.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) -MMD -MP $(CXXFLAGS) $(EXTRA) -c $< -o $@

$(BUILD)/%.s.o: $(REPO)/%.s
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ)
	@echo linking $@
	@$(CXX) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(BUILD)/$(TARGET).smdh: icon.png
	@smdhtool --create "$(TITLE)" "Pokemon companion: GBA, Game Boy, Game Boy Color" "PokeDaisy" icon.png $@

$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).smdh
	@3dsxtool $< $@ --smdh=$(BUILD)/$(TARGET).smdh
	@echo built $@

# The CIA: makerom packs the ELF with the permissions the recompiler needs. Those are armGBA-3DS's own
# (platform/3ds/armgba.rsf: the 80 MB mode, the 804 MHz clock, every core, svcControlProcessMemory), taken from the
# submodule with PokeDaisy's title, product code and unique ID in place of armGBA's. banner.png and banner.wav are
# drawn by ../tools/gen_banner.py (banner.png). cia-parts makes what bannertool makes; cia.sh then runs makerom (see ../Makefile).
BANNERTOOL ?= bannertool
MAKEROM ?= makerom
# The banner's sound: a second and a half of silence (32 kHz, stereo, 16-bit), made here - the app has no chime.
$(BUILD)/banner.wav:
	@mkdir -p $(BUILD)
	@printf 'RIFF\044\356\002\000WAVEfmt \020\000\000\000\001\000\002\000\000\175\000\000\000\364\001\000\004\000\020\000data\000\356\002\000' > $@
	@head -c 192000 /dev/zero >> $@
$(BUILD)/banner.bnr: banner.png $(BUILD)/banner.wav
	@$(BANNERTOOL) makebanner -i banner.png -a $(BUILD)/banner.wav -o $@
$(BUILD)/icon.icn: icon.png
	@$(BANNERTOOL) makesmdh -s "$(TITLE)" -l "Pokemon companion: GBA, Game Boy, Game Boy Color" -p "PokeDaisy" -i icon.png -o $@
$(BUILD)/pokedaisy.rsf: $(ARMGBA)/platform/3ds/armgba.rsf
	@sed -e 's/: armGBA$$/: PokeDaisy/' -e 's/CTR-H-AGBA/CTR-H-PKDY/' -e 's/0xFA6BA/0xFD41A/' $< > $@
.PHONY: cia-parts
cia-parts: $(BUILD)/$(TARGET).elf $(BUILD)/banner.bnr $(BUILD)/icon.icn $(BUILD)/pokedaisy.rsf
$(TARGET).cia: cia-parts
	@$(MAKEROM) -f cia -o $@ -elf $(BUILD)/$(TARGET).elf -rsf $(BUILD)/pokedaisy.rsf -target t -exefslogo -icon $(BUILD)/icon.icn \
	         -banner $(BUILD)/banner.bnr -major 0 -minor 1 -micro 0
	@echo built $@
cia: $(TARGET).cia

clean:
	rm -rf build-release build-dev build-test pokedaisy.3dsx pokedaisy-dev.3dsx pokedaisy-test.3dsx pokedaisy.cia

-include $(OBJ:.o=.d)
