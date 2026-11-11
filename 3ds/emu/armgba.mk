# armGBA's sources (third_party/armgba, a submodule pinned to its 3ds branch), as its own builds list them
# (CMakeLists.txt, platform/3ds/Makefile): the core with the recompiler for the CPU it's built for, the libretro layer
# the 3DS port drives it through, Gambatte for the Game Boy, and the archive readers that layer links.
# ARMGBA is the submodule's path; ARMGBA_BACKEND picks the recompiler: a32 (the 3DS), x64 (the simulator).
ARMGBA_CORE := bios-image.c bus.c cheats.c gba.c memory.c state.c timing.c \
	audio/audio.c audio/audio-thread.c audio/psg.c cart/cart.c cart/ereader.c cart/gpio.c cart/overrides.c cart/savedata.c \
	cpu/arm.c cpu/decode.c cpu/step.c io/dma.c io/io.c io/rfu.c io/sio.c io/timer.c \
	video/render-thread.c video/render.c video/video.c cpu/jit.c cpu/$(ARMGBA_BACKEND)/backend.c
ifeq ($(ARMGBA_BACKEND),x64)
ARMGBA_CORE += video/render-x86.c
endif
ARMGBA_CORE_SRC := $(addprefix $(ARMGBA)/src/,$(ARMGBA_CORE))
ARMGBA_LIBRETRO_SRC := $(addprefix $(ARMGBA)/libretro/,libretro.c clock.c lcd.c options.c saves.c dolphin.c link.c games.c \
	savefiles.c patch.c dmg07.c ereader.c printer.c printout.c workers.c gb-gambatte.cpp)
ARMGBA_GAMBATTE_SRC := $(filter-out %/file_zip.cpp %/cinterface.cpp,$(shell find $(ARMGBA)/gambatte/libgambatte/src -name '*.cpp' -not -path '*/unzip/*'))
ARMGBA_ARCHIVE_SRC := $(shell find $(ARMGBA)/libretro/deps/libretro-common $(ARMGBA)/libretro/deps/7zip -name '*.c')

ARMGBA_ARCHIVE_INC := -I$(ARMGBA)/libretro/deps/libretro-common/include -I$(ARMGBA)/libretro/deps
ARMGBA_GAMBATTE_INC := -I$(ARMGBA)/gambatte/libgambatte/include -I$(ARMGBA)/gambatte/libgambatte/src -I$(ARMGBA)/gambatte/common -DHAVE_STDINT_H
# -fwrapv: armGBA's clocks are 32-bit counters compared by subtracting, which signed overflow must allow (its 3DS
# README: without it every game hangs 128 seconds in).
ARMGBA_CORE_FLAGS := -O3 -fwrapv -Wall -Wextra -Wno-unused-parameter -I$(ARMGBA)/src
ARMGBA_LIBRETRO_FLAGS := -O2 -fwrapv -I$(ARMGBA)/src $(ARMGBA_ARCHIVE_INC) $(ARMGBA_GAMBATTE_INC)
ARMGBA_GAMBATTE_FLAGS := -O2 -w $(ARMGBA_GAMBATTE_INC)
ARMGBA_ARCHIVE_FLAGS := -O2 -w $(ARMGBA_ARCHIVE_INC) -DHAVE_ZLIB -DHAVE_7ZIP
# What PokeDaisy's own glue (emu/pd_emu.c, emu/pd_emu_call.c) needs to see of it.
ARMGBA_GLUE_INC := -I$(ARMGBA)/src -I$(ARMGBA)/libretro
