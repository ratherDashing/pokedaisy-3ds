// See ../../core/core.h: the seam's stand-in for mGBA's header of this name, not mGBA's own. `struct GBA` here is a
// view of the running armGBA machine in the member names the shared code reads (pk_wide.c): each is a pointer into
// armGBA's own memory, set by ../../../../pd_emu.c. Nothing may take sizeof of a member.
#ifndef PD_EMU_MGBA_GBA_H
#define PD_EMU_MGBA_GBA_H

#include <mgba/core/core.h>

#define BASE_WORKING_RAM 0x02000000u
#define BASE_WORKING_IRAM 0x03000000u
#define BASE_IO 0x04000000u
#define BASE_PALETTE_RAM 0x05000000u
#define BASE_VRAM 0x06000000u
#define BASE_OAM 0x07000000u
#define BASE_CART0 0x08000000u
#define SIZE_WORKING_RAM 0x40000
#define SIZE_WORKING_IRAM 0x8000

struct GBAVideoRenderer;
// mGBA's renderer calls, which pk_wide.c hooks to see each line as it is drawn. armGBA draws on other cores from a
// log of its own, so there is nothing to hook: these stay NULL and pk_wide.c keeps to the registers a frame ends with.
struct GBAVideoRenderer {
    void (*drawScanline)(struct GBAVideoRenderer*, int y);
    void (*writeVRAM)(struct GBAVideoRenderer*, uint32_t address);
    void (*writeOAM)(struct GBAVideoRenderer*, uint32_t oam);
    void (*writePalette)(struct GBAVideoRenderer*, uint32_t address, uint16_t value);
};

#define PD_EMU_GBA_VIEW \
    struct { \
        uint32_t* wram; \
        uint32_t* iwram; \
        uint32_t* rom; \
        size_t romSize; \
        uint16_t* io; \
    } memory; \
    struct { \
        uint16_t* palette; \
        uint16_t* vram; \
        struct { uint16_t* raw; } oam; \
        struct GBAVideoRenderer* renderer; \
    } video;

struct GBA {
    PD_EMU_GBA_VIEW
};

#endif
