// The 3DS port's emulator seam, in mGBA's names. This is NOT mGBA's header: the 3DS build and the simulator run armGBA
// (third_party/armgba; Gambatte for the Game Boy), and the code PokeDaisy shares with its mGBA hosts (linux/fly.c,
// linux/field_items.c, linux/gb_field.c, app/src/main/cpp/pk_wide*.c) talks to its core through `struct mCore`. This
// header declares only the members that code uses, and ../pd_emu.c fills them in over armGBA, so the shared files
// build unchanged for both. GPL-3.0 (see NOTICE); no mGBA code is in it.
#ifndef PD_EMU_MGBA_CORE_H
#define PD_EMU_MGBA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t color_t;

enum mPlatform { mPLATFORM_NONE = -1, mPLATFORM_GBA = 0, mPLATFORM_GB = 1 };
enum mCoreChecksumType { mCHECKSUM_CRC32 };

struct mCore {
    void* cpu;   // armGBA's own; nothing outside ../pd_emu*.c may look inside
    void* board; // a `struct GBA` / `struct GB` as the shim headers beside this one declare them: pointers into the core
    enum mPlatform (*platform)(const struct mCore*);
    void (*deinit)(struct mCore*);
    void (*reset)(struct mCore*);
    void (*setKeys)(struct mCore*, uint32_t keys);
    void (*runFrame)(struct mCore*);
    void (*step)(struct mCore*);
    void (*desiredVideoDimensions)(const struct mCore*, unsigned* width, unsigned* height);
    void (*setVideoBuffer)(struct mCore*, color_t* buffer, size_t stride);
    void (*getPixels)(struct mCore*, const void** buffer, size_t* stride);
    void (*checksum)(const struct mCore*, void* data, enum mCoreChecksumType type);
    void* (*getMemoryBlock)(struct mCore*, size_t id, size_t* sizeOut);
    uint32_t (*busRead8)(struct mCore*, uint32_t address);
    uint32_t (*busRead16)(struct mCore*, uint32_t address);
    uint32_t (*busRead32)(struct mCore*, uint32_t address);
    void (*busWrite8)(struct mCore*, uint32_t address, uint8_t value);
    void (*busWrite16)(struct mCore*, uint32_t address, uint16_t value);
    void (*busWrite32)(struct mCore*, uint32_t address, uint32_t value);
    uint32_t (*rawRead8)(struct mCore*, uint32_t address, int segment);
    uint32_t (*rawRead16)(struct mCore*, uint32_t address, int segment);
    uint32_t (*rawRead32)(struct mCore*, uint32_t address, int segment);
};

#endif
