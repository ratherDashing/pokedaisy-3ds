# pd_sim: the 3DS app's host logic (../../host), its companion (../../core) and its emulator (../../emu: armGBA with
# the x86-64 recompiler, Gambatte) with no screens. `make sim` in 3ds/ runs this; objects go in $(OUT).
REPO := $(abspath ../../..)
OUT ?= $(REPO)/3ds/build/sim
ARMGBA := $(REPO)/third_party/armgba
ARMGBA_BACKEND := x64
include $(REPO)/3ds/emu/armgba.mk

CC ?= cc
CXX ?= c++
D3 := $(REPO)/3ds
CPP := $(REPO)/app/src/main/cpp
PD_SRC := $(wildcard $(D3)/core/*.c) $(wildcard $(D3)/host/*.c) $(D3)/desktop/sim/pd_sim.c $(D3)/desktop/pd_png.c \
	$(CPP)/pk_wide.c $(CPP)/pk_wide_gb.c \
	$(REPO)/linux/fly.c $(REPO)/linux/field_items.c $(REPO)/linux/gb_field.c $(REPO)/linux/wild.c $(REPO)/linux/hud.c $(REPO)/linux/quick_menu.c
GLUE_SRC := $(D3)/emu/pd_emu.c $(D3)/emu/pd_emu_call.c

obj = $(patsubst $(REPO)/%,$(OUT)/obj/%.o,$(1))
PD_OBJ := $(call obj,$(PD_SRC))
GLUE_OBJ := $(call obj,$(GLUE_SRC))
CORE_OBJ := $(call obj,$(ARMGBA_CORE_SRC))
LIBRETRO_OBJ := $(call obj,$(ARMGBA_LIBRETRO_SRC))
GAMBATTE_OBJ := $(call obj,$(ARMGBA_GAMBATTE_SRC))
ARCHIVE_OBJ := $(call obj,$(ARMGBA_ARCHIVE_SRC))
OBJ := $(PD_OBJ) $(GLUE_OBJ) $(CORE_OBJ) $(LIBRETRO_OBJ) $(GAMBATTE_OBJ) $(ARCHIVE_OBJ)

# The seam's headers stand where mGBA's did (emu/include), so the code shared with the mGBA hosts builds unchanged.
PD_INC := -I$(D3)/emu/include -I$(D3)/core -I$(REPO)/linux -I$(CPP)
$(PD_OBJ):       EXTRA := -std=gnu11 -O2 -g -Wall -Wextra $(PD_INC) -DM_CORE_GBA -DM_CORE_GB -DPK_WIDE_CACHE
$(GLUE_OBJ):     EXTRA := -std=gnu11 -O2 -g -Wall -Wextra $(PD_INC) $(ARMGBA_GLUE_INC)
$(CORE_OBJ):     EXTRA := -std=gnu11 $(ARMGBA_CORE_FLAGS)
$(LIBRETRO_OBJ): EXTRA := $(ARMGBA_LIBRETRO_FLAGS)
$(GAMBATTE_OBJ): EXTRA := $(ARMGBA_GAMBATTE_FLAGS)
$(ARCHIVE_OBJ):  EXTRA := $(ARMGBA_ARCHIVE_FLAGS)

$(OUT)/pd_sim: $(OBJ)
	@echo linking $@
	@$(CXX) -g $(OBJ) -lz -lm -lpthread -o $@

$(OUT)/obj/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(EXTRA) -c $< -o $@

$(OUT)/obj/%.cpp.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -std=gnu++11 -fno-exceptions -fno-rtti $(EXTRA) -c $< -o $@

-include $(OBJ:.o=.d)
