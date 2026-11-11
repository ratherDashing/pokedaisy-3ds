// The 3DS port's emulator: armGBA for the Game Boy Advance (its recompiler, its renderer and mixer on other cores) and
// Gambatte for the Game Boy and Game Boy Color, both through armGBA's libretro layer (third_party/armgba), behind the
// `struct mCore` the rest of PokeDaisy's C talks to (include/mgba/core/core.h, which is this port's own, not mGBA's).
// One game at a time. GPL-3.0 (see NOTICE).
#ifndef PD_EMU_H
#define PD_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mCore;

// What both consoles' sound comes out at, stereo, before the host's own pacing.
#define PD_EMU_SAMPLE_RATE 32768
// A GBA frame, and a Game Boy's (the same length in time): 280896 cycles of a 16.78 MHz clock.
#define PD_EMU_FPS (16777216.0 / 280896.0)

struct pd_emu_host {
    // A frame's sound, as it is made (on the thread that runs the core).
    void (*audio)(void* ctx, const int16_t* stereo, size_t frames);
    // A line from the core or this layer; NULL: dropped.
    void (*log)(void* ctx, const char* line);
    void* ctx;
    // A folder that may hold gba_bios.bin (else armGBA's own BIOS is used); NULL: none.
    const char* systemDir;
    // A folder for what armGBA's libretro layer keeps for itself. Never the saves: those stay where PokeDaisy has them.
    const char* workDir;
    // The renderer and the mixer on cores of their own (a New 3DS; a desktop's threads in the simulator).
    bool threaded;
};

// Loads the game at romPath (.gba, .gb or .gbc) with its save at savePath (mGBA's layout: the cartridge's save memory,
// then the clock where the cartridge has one; made on the game's first save). NULL with a message in error if it can't.
struct mCore* pd_emu_load(const char* romPath, const char* savePath, const struct pd_emu_host* host, char* error, size_t errorLen);

// Whether the recompiler is running (false: armGBA's interpreter - no executable memory, or a build without it).
bool pd_emu_recompiling(struct mCore* core);
// Whether the renderer and mixer run on threads of their own.
bool pd_emu_threaded(struct mCore* core);

// The frames from now on aren't shown (fast-forward's extra ones): the core needn't draw them.
void pd_emu_skip_video(struct mCore* core, bool skip);

// Several bytes of the game's RAM at once (GBA: EWRAM / IWRAM), compiled code over them thrown out.
bool pd_emu_write(struct mCore* core, uint32_t addr, const void* data, size_t len);

enum pd_emu_state_result {
    PD_EMU_STATE_OK,
    PD_EMU_STATE_NO_FILE,
    PD_EMU_STATE_FOREIGN, // not this core's: an mGBA state from the earlier build, or anything else
    PD_EMU_STATE_OTHER_GAME,
    PD_EMU_STATE_BAD,     // this core's, but it didn't load (another version of the core)
};
bool pd_emu_save_state(struct mCore* core, const char* path);
// Loads a state; the cartridge's save memory stays as it is now (a state must never put an old save back).
enum pd_emu_state_result pd_emu_load_state(struct mCore* core, const char* path);

// Writes the save file now if the game has written to it since the last time.
void pd_emu_flush_save(struct mCore* core);

#endif
