// See ../../core/core.h: the seam's stand-in for mGBA's header of this name, not mGBA's own. `struct GB` is a view of
// the Game Boy running on Gambatte (through armGBA's libretro layer), in the member names the shared code reads
// (linux/gb_field.c, pk_wide_gb.c): pointers set by ../../../../pd_emu.c, wramBank after every frame.
#ifndef PD_EMU_MGBA_GB_H
#define PD_EMU_MGBA_GB_H

#include <mgba/core/core.h>

#define GB_SIZE_WORKING_RAM_BANK0 0x1000
#define GB_REG_LCDC 0x40
#define GB_REG_SCY 0x42
#define GB_REG_SCX 0x43
#define GB_REG_LY 0x44
#define GB_REG_WY 0x4A
#define GB_REG_WX 0x4B
#define GB_REG_SVBK 0x70

enum GBModel { GB_MODEL_DMG = 0x00, GB_MODEL_CGB = 0x80 };

struct GBVideoRenderer;
struct GBVideoRenderer {
    void (*writePalette)(struct GBVideoRenderer*, int index, uint16_t value);
    void (*writeVRAM)(struct GBVideoRenderer*, uint16_t address);
};

#define PD_EMU_GB_VIEW \
    struct { \
        uint8_t* wram;     /* every bank, 0x1000 each */ \
        uint8_t* wramBank; /* the one at D000 */ \
        uint8_t* hram;     /* FF80.. */ \
        uint8_t* io;       /* FF00.. */ \
        uint8_t* rom; \
        size_t romSize; \
    } memory; \
    struct { \
        uint8_t* vram; \
        struct { uint8_t* raw; } oam; \
        struct GBVideoRenderer* renderer; \
        int ly; \
    } video;

struct GB {
    PD_EMU_GB_VIEW
};

#endif
