// armGBA and Gambatte behind PokeDaisy's core seam: pd_emu.h. This file is the only one (with pd_emu_call.c) that sees
// armGBA's own headers; everything else reads the game through `struct mCore` and the views filled in here.
// GPL-3.0 (see NOTICE); armGBA itself is MPL-2.0 and Gambatte GPL-2.0, linked, not copied.
#include "pd_emu.h"
#include "pd_emu_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <mgba/core/core.h> // this port's own seam, not mGBA's

#include "../../app/src/main/cpp/pk_cheats.h"
#include "../../app/src/main/cpp/pk_gb_field.h"
#include "../../app/src/main/cpp/pk_host.h"

// armGBA (third_party/armgba/src, libretro)
#include "cpu/jit.h"
#include "gb.h"
#include "gba.h"
#include "libretro.h"

// armGBA's libretro layer (libretro/core.h): player 1's console, or a Game Boy game running.
extern struct GBA* gba;
extern bool gameBoy;
#if defined(__3DS__)
void jitWarmLoad(const char* path);
void jitWarmSave(const char* path);
#endif

// The views the shared code reads the machine through: the shim headers' `struct GBA` / `struct GB`, declared here
// under other names because armGBA has a `struct GBA` of its own.
struct GBAVideoRenderer {
    void (*drawScanline)(struct GBAVideoRenderer*, int y);
    void (*writeVRAM)(struct GBAVideoRenderer*, uint32_t address);
    void (*writeOAM)(struct GBAVideoRenderer*, uint32_t oam);
    void (*writePalette)(struct GBAVideoRenderer*, uint32_t address, uint16_t value);
};
struct pd_gba_view {
    struct {
        uint32_t* wram;
        uint32_t* iwram;
        uint32_t* rom;
        size_t romSize;
        uint16_t* io;
    } memory;
    struct {
        uint16_t* palette;
        uint16_t* vram;
        struct { uint16_t* raw; } oam;
        struct GBAVideoRenderer* renderer;
    } video;
};
struct GBVideoRenderer {
    void (*writePalette)(struct GBVideoRenderer*, int index, uint16_t value);
    void (*writeVRAM)(struct GBVideoRenderer*, uint16_t address);
};
struct pd_gb_renderer {
    struct GBVideoRenderer d;
    uint16_t palette[192];
    uint8_t lookup[192];
    int model;
};
struct pd_gb_view {
    struct {
        uint8_t* wram;
        uint8_t* wramBank;
        uint8_t* hram;
        uint8_t* io;
        uint8_t* rom;
        size_t romSize;
    } memory;
    struct {
        uint8_t* vram;
        struct { uint8_t* raw; } oam;
        struct GBVideoRenderer* renderer;
        int ly;
    } video;
};

#define STATE_MAGIC_TEXT "PDaG"
#define STATE_VERSION 1
#define SAVE_QUIET_FRAMES 15 // a save's writes have stopped this long: the file is written (mGBA's own wait)
#define GBA_RTC_TRAILER 16   // mGBA's GBASavedataRTCBuffer after the save data
#define GB_RTC_TRAILER 48    // mGBA's GBMBCRTCSaveBuffer after the cartridge RAM
// armGBA's .rtc memory for an MBC3 (libretro/gb-gambatte.cpp): the five registers as 32-bit words, then when they were so.
#define GB_RTC_REGS 20
#define GB_RTC_SAVED 40

static struct {
    struct mCore core;
    bool loaded, gb, cgb;
    struct pd_emu_host host;
    struct pd_gba_view gbaView;
    struct pd_gb_view gbView;
    struct GBAVideoRenderer gbaRenderer;
    struct pd_gb_renderer gbRenderer;
    char savePath[1024], romPath[1024], warmPath[1100];
    uint8_t* romCopy; // the Game Boy cart as read (Gambatte keeps its own copy; this one answers the checksum)
    size_t romSize;
    uint32_t romCrc;
    uint8_t* gbCartRam;
    size_t gbCartRamSize;
    uint8_t* gbOamBlock; // FE00..FFFF as Gambatte keeps it: OAM, then I/O at +0x100, HRAM at +0x180
    uint32_t keys;
    color_t* video;
    size_t stride;
    unsigned width, height;
    bool skipVideo;
    // The save file.
    bool savePending;
    int saveQuiet;
    uint32_t gbSaveHash;
    unsigned frames;
    bool hadTrailer;
    const struct retro_core_option_v2_definition* options;
    retro_audio_buffer_status_callback_t audioStatus;
    enum retro_pixel_format format;
} E;

static void emu_log(const char* format, ...) {
    if (!E.host.log) return;
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    line[strcspn(line, "\n")] = 0;
    E.host.log(E.host.ctx, line);
}

uint32_t doCrc32(const void* buf, size_t size) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = buf;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// --- the libretro side: what armGBA's layer asks its frontend ---

// The options that differ from armGBA's defaults. Threaded Video and Audio are armGBA-3DS's own settings on a New 3DS.
// Frame Skip is Auto only so that fast-forward's unseen frames can be left undrawn (soundStatus below): a frame that
// is shown is always drawn.
static const char* option_value(const char* key) {
    static const struct { const char* key; const char* value; } FIXED[] = {
        { "armgba_skip_bios", "enabled" },
        { "armgba_gb_boot_rom", "disabled" },
        { "armgba_frame_skip", "auto" },
        { "armgba_restart_buttons", "off" },
        { "armgba_clock_source", "real" },
    };
    if (!strcmp(key, "armgba_threaded_video") || !strcmp(key, "armgba_threaded_audio")) return E.host.threaded ? "enabled" : "disabled";
    for (size_t i = 0; i < sizeof(FIXED) / sizeof(*FIXED); i++)
        if (!strcmp(key, FIXED[i].key)) return FIXED[i].value;
    for (const struct retro_core_option_v2_definition* d = E.options; d && d->key; d++)
        if (!strcmp(key, d->key)) return d->default_value;
    return NULL;
}

static void core_log(enum retro_log_level level, const char* format, ...) {
    if (!E.host.log || level < RETRO_LOG_INFO) return;
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    emu_log("core: %s", line);
}

static bool environment(unsigned cmd, void* data) {
    switch (cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback*) data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char**) data = E.host.systemDir ? E.host.systemDir : E.host.workDir;
        return *(const char**) data != NULL;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char**) data = E.host.workDir;
        return E.host.workDir != NULL;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        E.format = *(const enum retro_pixel_format*) data;
        return E.format == RETRO_PIXEL_FORMAT_RGB565 || E.format == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned*) data = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        if (data) E.options = ((const struct retro_core_options_v2*) data)->definitions;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = data;
        v->value = v->key ? option_value(v->key) : NULL;
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool*) data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        E.audioStatus = data ? ((const struct retro_audio_buffer_status_callback*) data)->callback : NULL;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int*) data = 3;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const struct retro_message* m = data;
        if (m && m->msg) emu_log("core says: %s", m->msg);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLE:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        return true;
    default:
        return false; // no rumble, netplay or link here
    }
}

// The core's picture into PokeDaisy's buffer: 0xXXBBGGRR, as every screen mode and the thumbnails read it.
static void video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data || !E.video || E.skipVideo) return;
    if (width > E.stride) width = (unsigned) E.stride;
    E.width = width;
    E.height = height;
    if (E.format == RETRO_PIXEL_FORMAT_RGB565) {
        for (unsigned y = 0; y < height; y++) {
            const uint16_t* in = (const uint16_t*) ((const uint8_t*) data + y * pitch);
            color_t* out = E.video + y * E.stride;
            for (unsigned x = 0; x < width; x++) {
                // armGBA's 5-6-5 is the GBA's 5-5-5 with green's low bit a copy of its high one: back to five bits a
                // channel, each widened to eight as mGBA's 32-bit renderer did (c << 3 | c >> 2).
                uint32_t c = in[x];
                uint32_t wide = (c >> 8 & 0xF8) | (c << 5 & 0xF800) | (c << 19 & 0xF80000);
                out[x] = wide | (wide >> 5 & 0x070707);
            }
        }
    } else {
        for (unsigned y = 0; y < height; y++) {
            const uint32_t* in = (const uint32_t*) ((const uint8_t*) data + y * pitch);
            color_t* out = E.video + y * E.stride;
            for (unsigned x = 0; x < width; x++) {
                uint32_t c = in[x];
                out[x] = (c >> 16 & 0xFF) | (c & 0xFF00) | (c << 16 & 0xFF0000);
            }
        }
    }
}

static size_t audio_batch(const int16_t* data, size_t frames) {
    if (E.host.audio && frames) E.host.audio(E.host.ctx, data, frames);
    return frames;
}

static void audio_sample(int16_t left, int16_t right) {
    int16_t pair[2] = { left, right };
    audio_batch(pair, 1);
}

static void input_poll(void) {
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void) index;
    if (port || device != RETRO_DEVICE_JOYPAD) return 0;
    // PokeDaisy's keys are KEYINPUT's bits.
    static const int8_t BIT[16] = {
        [RETRO_DEVICE_ID_JOYPAD_A] = 1, [RETRO_DEVICE_ID_JOYPAD_B] = 2, [RETRO_DEVICE_ID_JOYPAD_SELECT] = 3, [RETRO_DEVICE_ID_JOYPAD_START] = 4,
        [RETRO_DEVICE_ID_JOYPAD_RIGHT] = 5, [RETRO_DEVICE_ID_JOYPAD_LEFT] = 6, [RETRO_DEVICE_ID_JOYPAD_UP] = 7, [RETRO_DEVICE_ID_JOYPAD_DOWN] = 8,
        [RETRO_DEVICE_ID_JOYPAD_R] = 9, [RETRO_DEVICE_ID_JOYPAD_L] = 10,
    };
    return id < 16 && BIT[id] ? (E.keys >> (BIT[id] - 1)) & 1 : 0;
}

// --- the views ---

static void refresh_views(void) {
    if (E.gb) {
        struct pd_gb_view* v = &E.gbView;
        if (!v->memory.wram) return;
        int bank = E.cgb ? v->memory.io[0x70] & 7 : 1;
        v->memory.wramBank = v->memory.wram + (bank ? bank : 1) * 0x1000;
        v->video.ly = v->memory.io[0x44];
        return;
    }
    struct pd_gba_view* v = &E.gbaView;
    v->memory.wram = (uint32_t*) gba->ewram;
    v->memory.iwram = (uint32_t*) gba->iwram;
    v->memory.rom = (uint32_t*) gba->rom;
    v->memory.romSize = gba->romSize;
    v->memory.io = gba->io;
    v->video.palette = (uint16_t*) gba->palette;
    v->video.vram = (uint16_t*) gba->vram;
    v->video.oam.raw = (uint16_t*) gba->oam;
    v->video.renderer = &E.gbaRenderer;
    E.core.cpu = &gba->cpu;
}

static void gb_no_palette(struct GBVideoRenderer* r, int index, uint16_t value) {
    (void) r; (void) index; (void) value;
}

static void gb_no_vram(struct GBVideoRenderer* r, uint16_t address) {
    (void) r; (void) address;
}

static bool gb_map(void) {
    struct retro_memory_descriptor areas[10];
    size_t n = libretroGbMemoryMap(areas);
    struct pd_gb_view* v = &E.gbView;
    memset(v, 0, sizeof(*v));
    E.gbCartRam = NULL;
    for (size_t i = 0; i < n; i++) {
        uint8_t* at = (uint8_t*) areas[i].ptr + areas[i].offset;
        switch (areas[i].start) {
        case 0xC000: v->memory.wram = at; break;
        case 0x8000: v->video.vram = at; break;
        case 0xFE00: v->video.oam.raw = at; E.gbOamBlock = at; break;
        case 0x0000: v->memory.rom = at; break;
        case 0xA000: E.gbCartRam = at; break;
        default: break;
        }
    }
    if (!v->memory.wram || !E.gbOamBlock || !v->memory.rom) return false;
    v->memory.io = E.gbOamBlock + 0x100;
    v->memory.hram = E.gbOamBlock + 0x180;
    v->memory.romSize = E.romSize;
    size_t ramSize = 0;
    uint8_t* ram = libretroGbSaveRam(&ramSize);
    E.gbCartRam = ram;
    E.gbCartRamSize = ramSize;
    E.cgb = libretroGbIsColor();
    struct pd_gb_renderer* r = &E.gbRenderer;
    r->d.writePalette = gb_no_palette;
    r->d.writeVRAM = gb_no_vram;
    r->model = E.cgb ? 0x80 : 0;
    for (int i = 0; i < 192; i++) {
        // White to black, four to a palette, in 5-6-5.
        static const uint16_t GREY[4] = { 0xFFFF, 0xAD55, 0x52AA, 0x0000 };
        r->lookup[i] = (uint8_t) i;
        r->palette[i] = GREY[i & 3];
    }
    v->video.renderer = &r->d;
    return true;
}

// --- the save file ---

static uint32_t hash_of(const uint8_t* data, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * 16777619u;
    return h;
}

static void put32(uint8_t* to, uint32_t v) {
    for (int i = 0; i < 4; i++) to[i] = (uint8_t) (v >> (i * 8));
}

static void put64(uint8_t* to, uint64_t v) {
    for (int i = 0; i < 8; i++) to[i] = (uint8_t) (v >> (i * 8));
}

static uint32_t get32(const uint8_t* from) {
    return (uint32_t) from[0] | (uint32_t) from[1] << 8 | (uint32_t) from[2] << 16 | (uint32_t) from[3] << 24;
}

static uint64_t get64(const uint8_t* from) {
    return (uint64_t) get32(from) | (uint64_t) get32(from + 4) << 32;
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = length > 0 ? malloc((size_t) length) : NULL;
    if (data && fread(data, 1, (size_t) length, f) != (size_t) length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (size_t) length;
    return data;
}

// The whole file to a new one first, then over the old: a crash or a full card can't leave half a save.
static bool write_file(const char* path, const uint8_t* a, size_t aLen, const uint8_t* b, size_t bLen) {
    char temp[1100];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE* f = fopen(temp, "wb");
    if (!f) return false;
    bool ok = fwrite(a, 1, aLen, f) == aLen && (!bLen || fwrite(b, 1, bLen, f) == bLen);
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        remove(temp);
        return false;
    }
#if defined(__3DS__)
    remove(path); // the SD card's rename won't replace a file
#endif
    return rename(temp, path) == 0;
}

static void read_save(void) {
    size_t size = 0;
    uint8_t* file = read_file(E.savePath, &size);
    if (!file) {
        // A write that was cut off between the old file going and the new one's rename.
        char temp[1100];
        snprintf(temp, sizeof(temp), "%s.tmp", E.savePath);
        file = read_file(temp, &size);
        if (file) emu_log("save: %s is missing, read its .tmp", E.savePath);
    }
    E.hadTrailer = false;
    if (E.gb) {
        if (file && E.gbCartRam && E.gbCartRamSize) {
            size_t n = size < E.gbCartRamSize ? size : E.gbCartRamSize;
            memcpy(E.gbCartRam, file, n);
            // mGBA's clock after the RAM: the registers it latched and when. Handed over as armGBA reads mGBA's own
            // .rtc, so the clock moves on by the time the game was off.
            size_t rtcSize = 0;
            uint8_t* rtc = libretroGbRtc(&rtcSize);
            if (rtc && rtcSize >= GB_RTC_SAVED + 8 && size >= E.gbCartRamSize + GB_RTC_TRAILER - 4) {
                const uint8_t* t = file + E.gbCartRamSize;
                memset(rtc, 0, rtcSize);
                memcpy(rtc + GB_RTC_REGS, t + 20, 20); // the latched five
                if (size >= E.gbCartRamSize + GB_RTC_TRAILER) memcpy(rtc + GB_RTC_SAVED, t + 40, 8);
                else put32(rtc + GB_RTC_SAVED, get32(t + 40));
                E.hadTrailer = true;
            }
            emu_log("save: read %u bytes of %u from %s%s", (unsigned) n, (unsigned) E.gbCartRamSize, E.savePath, E.hadTrailer ? " (with its clock)" : "");
        }
        E.gbSaveHash = E.gbCartRam ? hash_of(E.gbCartRam, E.gbCartRamSize) : 0;
    } else if (file) {
        // As large as the largest kind until the game shows which it has; a file is as long as its kind.
        size_t n = size < sizeof(gba->savedata.storage) ? size : sizeof(gba->savedata.storage);
        size_t known = savedataSize(gba);
        if (known && n > known) n = known;
        if (!known && n > GBA_RTC_TRAILER && (n & 0x1FF) == GBA_RTC_TRAILER) n -= GBA_RTC_TRAILER;
        memcpy(gba->savedata.storage, file, n);
        // mGBA's clock after the data: only its control byte says anything (24-hour, battery); the time is the device's.
        if ((gba->gpio.devices & HW_RTC) && size >= n + GBA_RTC_TRAILER) {
            gba->gpio.rtc.control = file[n + 7];
            E.hadTrailer = true;
        }
        gba->savedata.dirty = false;
        emu_log("save: read %u bytes from %s", (unsigned) n, E.savePath);
    }
    free(file);
    E.savePending = false;
    E.saveQuiet = 0;
}

static bool write_save(void) {
    bool ok = false;
    if (E.gb) {
        if (!E.gbCartRam || !E.gbCartRamSize) return true;
        uint8_t trailer[GB_RTC_TRAILER];
        size_t rtcSize = 0;
        uint8_t* rtc = libretroGbRtc(&rtcSize);
        bool clock = rtc && rtcSize >= GB_RTC_SAVED + 8 && libretroGbClock() == LIBRETRO_GB_MBC3_CLOCK;
        if (clock) {
            // mGBA's layout: the registers now, the registers latched, the time.
            memcpy(trailer, rtc + GB_RTC_REGS, 20);
            memcpy(trailer + 20, rtc + GB_RTC_REGS, 20);
            memcpy(trailer + 40, rtc + GB_RTC_SAVED, 8);
        }
        ok = write_file(E.savePath, E.gbCartRam, E.gbCartRamSize, trailer, clock ? sizeof(trailer) : 0);
        if (ok) E.gbSaveHash = hash_of(E.gbCartRam, E.gbCartRamSize);
    } else {
        size_t size = savedataSize(gba);
        if (!size) return true; // the game never touched its save memory: nothing to keep
        uint8_t trailer[GBA_RTC_TRAILER] = { 0 };
        bool clock = (gba->gpio.devices & HW_RTC) != 0;
        if (clock) {
            memcpy(trailer, gba->gpio.rtc.time, 7);
            trailer[7] = gba->gpio.rtc.control;
            put64(trailer + 8, (uint64_t) time(NULL));
        }
        ok = write_file(E.savePath, gba->savedata.storage, size, trailer, clock ? sizeof(trailer) : 0);
    }
    emu_log(ok ? "save: wrote %s" : "save: COULDN'T WRITE %s", E.savePath);
    if (ok) {
        E.savePending = false;
        E.saveQuiet = 0;
    }
    return ok;
}

// After each frame: the game's writes to its save memory, and the file once they've stopped.
static void track_save(void) {
    if (E.gb) {
        // Gambatte has no flag for it: the cartridge RAM is looked at every few frames.
        if (E.gbCartRam && E.gbCartRamSize && (E.frames & 7) == 0) {
            uint32_t h = hash_of(E.gbCartRam, E.gbCartRamSize);
            if (h != E.gbSaveHash) {
                E.gbSaveHash = h;
                E.savePending = true;
                E.saveQuiet = 0;
            }
        }
    } else if (gba->savedata.dirty) {
        gba->savedata.dirty = false;
        E.savePending = true;
        E.saveQuiet = 0;
    }
    if (E.savePending && ++E.saveQuiet >= SAVE_QUIET_FRAMES) write_save();
}

void pd_emu_flush_save(struct mCore* core) {
    (void) core;
    if (!E.loaded) return;
    if (!E.gb && gba->savedata.dirty) {
        gba->savedata.dirty = false;
        E.savePending = true;
    }
    if (E.gb && E.gbCartRam && hash_of(E.gbCartRam, E.gbCartRamSize) != E.gbSaveHash) E.savePending = true;
    if (E.savePending) write_save();
}

// --- struct mCore ---

static enum mPlatform core_platform(const struct mCore* core) {
    (void) core;
    return E.gb ? mPLATFORM_GB : mPLATFORM_GBA;
}

static void core_set_keys(struct mCore* core, uint32_t keys) {
    (void) core;
    E.keys = keys;
}

static void core_run_frame(struct mCore* core) {
    (void) core;
    // Frame Skip's Auto leaves a frame undrawn while the sound is said to be running short: said only for the frames
    // fast-forward doesn't show.
    if (E.audioStatus) E.audioStatus(true, E.skipVideo ? 0 : 100, E.skipVideo);
    retro_run();
    E.frames++;
    refresh_views();
    track_save();
}

static void core_step(struct mCore* core) {
    (void) core;
    if (E.gb) return;
    pd_emu_step(gba);
}

static void core_reset(struct mCore* core) {
    (void) core;
    retro_reset();
    refresh_views();
}

static void core_dimensions(const struct mCore* core, unsigned* width, unsigned* height) {
    (void) core;
    *width = E.gb ? LIBRETRO_GB_WIDTH : 240;
    *height = E.gb ? LIBRETRO_GB_HEIGHT : 160;
}

static void core_set_video(struct mCore* core, color_t* buffer, size_t stride) {
    (void) core;
    E.video = buffer;
    E.stride = stride;
}

// armGBA's layer hands a frame over whole (its render threads have finished it), so there's nothing to wait for here.
static void core_get_pixels(struct mCore* core, const void** buffer, size_t* stride) {
    (void) core;
    *buffer = E.video;
    *stride = E.stride;
}

static void core_checksum(const struct mCore* core, void* data, enum mCoreChecksumType type) {
    (void) core; (void) type;
    *(uint32_t*) data = E.romCrc;
}

static void* core_memory_block(struct mCore* core, size_t id, size_t* sizeOut) {
    (void) core;
    *sizeOut = 0;
    if (E.gb) return NULL;
    switch (id) {
    case 2: *sizeOut = SIZE_EWRAM; return gba->ewram;
    case 3: *sizeOut = SIZE_IWRAM; return gba->iwram;
    case 8: *sizeOut = gba->romSize; return gba->rom;
    default: return NULL;
    }
}

static uint8_t* gb_at(uint32_t a) {
    struct pd_gb_view* v = &E.gbView;
    if (a < 0x4000) return a < v->memory.romSize ? v->memory.rom + a : NULL;
    if (a < 0x8000) return NULL; // which bank is switched in is Gambatte's to know
    if (a < 0xA000) return v->video.vram ? v->video.vram + (E.cgb ? (v->memory.io[0x4F] & 1) * 0x2000 : 0) + (a - 0x8000) : NULL;
    if (a < 0xC000) return NULL; // the same for the cartridge's RAM
    if (a < 0xD000) return v->memory.wram + (a - 0xC000);
    if (a < 0xE000) return v->memory.wramBank + (a - 0xD000);
    if (a < 0xFE00) return gb_at(a - 0x2000);
    if (a < 0x10000) return E.gbOamBlock + (a - 0xFE00);
    return NULL;
}

static uint32_t core_read8(struct mCore* core, uint32_t address) {
    (void) core;
    if (E.gb) {
        const uint8_t* p = gb_at(address & 0xFFFF);
        return p ? *p : 0xFF;
    }
    const uint8_t* p = codePointer(gba, address);
    if (p && (address >> 24) != REGION_BIOS) return *p;
    return memoryView32(gba, address & ~3u) >> ((address & 3) * 8) & 0xFF;
}

static uint32_t core_read16(struct mCore* core, uint32_t address) {
    return core_read8(core, address) | core_read8(core, address + 1) << 8;
}

static uint32_t core_read32(struct mCore* core, uint32_t address) {
    return core_read16(core, address) | core_read16(core, address + 2) << 16;
}

static uint32_t core_raw8(struct mCore* core, uint32_t address, int segment) {
    (void) segment;
    return core_read8(core, address);
}

static uint32_t core_raw16(struct mCore* core, uint32_t address, int segment) {
    (void) segment;
    return core_read16(core, address);
}

static uint32_t core_raw32(struct mCore* core, uint32_t address, int segment) {
    (void) segment;
    return core_read32(core, address);
}

static void core_write8(struct mCore* core, uint32_t address, uint8_t value) {
    (void) core;
    if (E.gb) {
        // RAM only: a write to a register or the cartridge would have to go through Gambatte's bus.
        address &= 0xFFFF;
        if ((address >= 0xC000 && address < 0xFE00) || (address >= 0xFF80 && address < 0xFFFF)) *gb_at(address) = value;
        return;
    }
    int cycles = 0;
    memoryStore8(gba, address, value, &cycles);
}

static void core_write16(struct mCore* core, uint32_t address, uint16_t value) {
    if (E.gb) {
        core_write8(core, address, (uint8_t) value);
        core_write8(core, address + 1, (uint8_t) (value >> 8));
        return;
    }
    int cycles = 0;
    memoryStore16(gba, address, value, &cycles);
}

static void core_write32(struct mCore* core, uint32_t address, uint32_t value) {
    if (E.gb) {
        core_write16(core, address, (uint16_t) value);
        core_write16(core, address + 2, (uint16_t) (value >> 16));
        return;
    }
    int cycles = 0;
    memoryStore32(gba, address, value, &cycles);
}

bool pd_emu_write(struct mCore* core, uint32_t addr, const void* data, size_t len) {
    (void) core;
    if (!E.loaded || E.gb) return false;
    uint8_t* to;
    if (addr >= 0x02000000 && addr - 0x02000000 + len <= SIZE_EWRAM) to = gba->ewram + (addr - 0x02000000);
    else if (addr >= 0x03000000 && addr - 0x03000000 + len <= SIZE_IWRAM) to = gba->iwram + (addr - 0x03000000);
    else return false;
    memcpy(to, data, len);
    jitInvalidate(gba, addr, (uint32_t) len);
    return true;
}

static void core_deinit(struct mCore* core) {
    (void) core;
    if (!E.loaded) return;
    pd_emu_flush_save(core);
    // A Game Boy cartridge's clock has moved on even if its RAM hasn't changed: the file's trailer follows it.
    if (E.gb && E.hadTrailer) write_save();
#if defined(__3DS__)
    if (!E.gb && E.warmPath[0]) jitWarmSave(E.warmPath);
#endif
    retro_unload_game();
    retro_deinit();
    free(E.romCopy);
    E.romCopy = NULL;
    E.loaded = false;
    E.video = NULL;
}

void pd_emu_skip_video(struct mCore* core, bool skip) {
    (void) core;
    E.skipVideo = skip;
}

bool pd_emu_recompiling(struct mCore* core) {
    (void) core;
    return E.loaded && !E.gb && gba->jit != NULL;
}

bool pd_emu_threaded(struct mCore* core) {
    (void) core;
    return E.loaded && !E.gb && gba->renderThread != NULL;
}

static bool is_gb_name(const char* path) {
    size_t n = strlen(path);
    return (n > 3 && !strcasecmp(path + n - 3, ".gb")) || (n > 4 && !strcasecmp(path + n - 4, ".gbc"));
}

struct mCore* pd_emu_load(const char* romPath, const char* savePath, const struct pd_emu_host* host, char* error, size_t errorLen) {
    if (E.loaded) {
        snprintf(error, errorLen, "a game is already running");
        return NULL;
    }
    memset(&E, 0, sizeof(E));
    E.host = *host;
    snprintf(E.romPath, sizeof(E.romPath), "%s", romPath);
    snprintf(E.savePath, sizeof(E.savePath), "%s", savePath);
    bool gb = is_gb_name(romPath);

    FILE* f = fopen(romPath, "rb");
    if (!f) {
        snprintf(error, errorLen, "can't open the game");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length < 0x150 || length > SIZE_ROM) {
        fclose(f);
        snprintf(error, errorLen, "not a game (%ld bytes)", length);
        return NULL;
    }
    uint8_t* rom;
    bool offered = false;
#if defined(__3DS__)
    // Into a buffer armGBA takes over as its cartridge: a 32 MB game is never in memory twice.
    if (!gb) {
        rom = calloc(1, gbaRomBufferSize((size_t) length));
        offered = rom != NULL;
    } else
#endif
    rom = malloc((size_t) length);
    if (!rom || fread(rom, 1, (size_t) length, f) != (size_t) length) {
        fclose(f);
        free(rom);
        snprintf(error, errorLen, "no memory for the game (%ld KiB), or it couldn't be read", length >> 10);
        return NULL;
    }
    fclose(f);
    E.romSize = (size_t) length;
    E.romCrc = doCrc32(rom, E.romSize);

    retro_set_environment(environment);
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    struct retro_game_info info = { .path = romPath, .data = rom, .size = E.romSize, .meta = NULL };
#if defined(__3DS__)
    if (offered) gbaOfferRom(rom);
#endif
    bool ok = retro_load_game(&info);
#if defined(__3DS__)
    if (offered) {
        if (gbaOfferedRom() == NULL) rom = NULL; // armGBA's now, and it frees it
        gbaOfferRom(NULL);
    }
#endif
    (void) offered;
    if (ok && gameBoy != gb) {
        // The layer goes by the cartridge's header too; what it decided is what runs.
        gb = gameBoy;
    }
    if (!ok || (!gb && !gba)) {
        if (ok) retro_unload_game();
        retro_deinit();
        free(rom);
        snprintf(error, errorLen, "the core couldn't start it");
        return NULL;
    }
    E.gb = gb;
    if (gb) {
        E.romCopy = rom;
        if (!gb_map()) {
            retro_unload_game();
            retro_deinit();
            free(rom);
            E.romCopy = NULL;
            snprintf(error, errorLen, "the Game Boy core gave no memory");
            return NULL;
        }
        E.core.board = &E.gbView;
    } else {
        free(rom); // armGBA has its own copy, or took this one over
        E.core.board = &E.gbaView;
    }
    E.loaded = true;

    struct mCore* c = &E.core;
    c->platform = core_platform;
    c->deinit = core_deinit;
    c->reset = core_reset;
    c->setKeys = core_set_keys;
    c->runFrame = core_run_frame;
    c->step = core_step;
    c->desiredVideoDimensions = core_dimensions;
    c->setVideoBuffer = core_set_video;
    c->getPixels = core_get_pixels;
    c->checksum = core_checksum;
    c->getMemoryBlock = core_memory_block;
    c->busRead8 = core_read8;
    c->busRead16 = core_read16;
    c->busRead32 = core_read32;
    c->busWrite8 = core_write8;
    c->busWrite16 = core_write16;
    c->busWrite32 = core_write32;
    c->rawRead8 = core_raw8;
    c->rawRead16 = core_raw16;
    c->rawRead32 = core_raw32;
    refresh_views();
    read_save();
    if (!gb) {
        emu_log("armGBA: %s, %s, %u KiB game", gba->jit ? "recompiler" : "interpreter (no recompiler)",
                gba->renderThread ? "renderer and mixer on their own cores" : "one thread", (unsigned) (gba->romSize >> 10));
#if defined(__3DS__)
        if (E.host.workDir) {
            snprintf(E.warmPath, sizeof(E.warmPath), "%s/%08lX.warm", E.host.workDir, (unsigned long) E.romCrc);
            jitWarmLoad(E.warmPath);
        }
#endif
    } else {
        emu_log("Gambatte: %s, %u KiB game, %u KiB of cartridge RAM", E.cgb ? "Game Boy Color" : "Game Boy", (unsigned) (E.romSize >> 10),
                (unsigned) (E.gbCartRamSize >> 10));
    }
    return c;
}

// --- states ---

struct state_header {
    char magic[4];
    uint32_t version;
    uint32_t platform;
    uint32_t romCrc;
    uint32_t size;
};

bool pd_emu_save_state(struct mCore* core, const char* path) {
    (void) core;
    if (!E.loaded) return false;
    size_t size = retro_serialize_size();
    uint8_t* data = size ? malloc(size) : NULL;
    if (!data || !retro_serialize(data, size)) {
        free(data);
        return false;
    }
    struct state_header h = { .version = STATE_VERSION, .platform = E.gb, .romCrc = E.romCrc, .size = (uint32_t) size };
    memcpy(h.magic, STATE_MAGIC_TEXT, 4);
    bool ok = write_file(path, (const uint8_t*) &h, sizeof(h), data, size);
    free(data);
    return ok;
}

enum pd_emu_state_result pd_emu_load_state(struct mCore* core, const char* path) {
    (void) core;
    if (!E.loaded) return PD_EMU_STATE_BAD;
    size_t size = 0;
    uint8_t* file = read_file(path, &size);
    if (!file) return PD_EMU_STATE_NO_FILE;
    struct state_header h;
    enum pd_emu_state_result result = PD_EMU_STATE_OK;
    if (size < sizeof(h) || memcmp(file, STATE_MAGIC_TEXT, 4)) {
        result = PD_EMU_STATE_FOREIGN;
    } else {
        memcpy(&h, file, sizeof(h));
        if (h.version != STATE_VERSION || h.platform != (uint32_t) E.gb) result = PD_EMU_STATE_FOREIGN;
        else if (h.romCrc != E.romCrc) result = PD_EMU_STATE_OTHER_GAME;
        else if (h.size != size - sizeof(h) || h.size != retro_serialize_size()) result = PD_EMU_STATE_BAD;
    }
    if (result != PD_EMU_STATE_OK) {
        free(file);
        return result;
    }
    // The save as it is now, kept through the load: armGBA's and Gambatte's states hold the cartridge's save memory,
    // and an old copy of it must never reach the save file (CLAUDE.md: save files are sacred).
    uint8_t* kept = NULL;
    size_t keptSize = E.gb ? E.gbCartRamSize : sizeof(gba->savedata.storage);
    const uint8_t* live = E.gb ? E.gbCartRam : gba->savedata.storage;
    if (live && keptSize) {
        kept = malloc(keptSize);
        if (!kept) {
            free(file);
            return PD_EMU_STATE_BAD;
        }
        memcpy(kept, live, keptSize);
    }
    bool pending = E.savePending;
    bool ok = retro_unserialize(file + sizeof(h), h.size);
    free(file);
    if (E.gb) gb_map();
    refresh_views();
    if (kept) {
        uint8_t* to = E.gb ? E.gbCartRam : gba->savedata.storage;
        if (to) memcpy(to, kept, keptSize);
        free(kept);
    }
    if (!E.gb) gba->savedata.dirty = false;
    else if (E.gbCartRam) E.gbSaveHash = hash_of(E.gbCartRam, E.gbCartRamSize);
    E.savePending = pending;
    return ok ? PD_EMU_STATE_OK : PD_EMU_STATE_BAD;
}

// --- pk_host.h, for this core ---

int pk_save_dirty(struct mCore* core) {
    (void) core;
    if (!E.loaded) return 0;
    return E.savePending || (!E.gb && gba->savedata.dirty);
}

int pk_finish_save(struct mCore* core, int maxFrames, void (*afterFrame)(void)) {
    int n = 0;
    if (!E.loaded) return 0;
    // A Game Boy's save is seen only every few frames: a few are run first, so one just begun is noticed.
    int lead = E.gb ? 16 : 0;
    if (pk_save_dirty(core) || lead) core->setKeys(core, 0);
    bool skip = E.skipVideo;
    E.skipVideo = true;
    while (n < maxFrames && (pk_save_dirty(core) || n < lead)) {
        core->runFrame(core);
        if (afterFrame) afterFrame();
        n++;
    }
    E.skipVideo = skip;
    return n;
}

void pk_heal_rtc(struct mCore* core) {
    (void) core;
    if (!E.loaded || E.gb || !(gba->gpio.devices & HW_RTC)) return;
    const char* code = gba->rom && gba->romSize > 0xB0 ? (const char*) gba->rom + 0xAC : "";
    bool pokemon = !strncmp(code, "AXV", 3) || !strncmp(code, "AXP", 3) || !strncmp(code, "BPE", 3);
    uint8_t control = gba->gpio.rtc.control;
    if (!(control & 0x80) && ((control & 0x40) || !pokemon)) return;
    emu_log("RTC from the save: control 0x%02x (%s), reset to a running 24-hour clock", control, control & 0x80 ? "battery dead" : "12-hour");
    gba->gpio.rtc.control = 0x40;
}

// --- pk_cheats.h: mGBA's .cheats file, its enabled sets given to armGBA's cheat engine ---

void pk_cheats_clear(struct mCore* core) {
    (void) core;
    if (E.loaded) retro_cheat_reset();
}

int pk_cheats_apply(struct mCore* core, const char* text, size_t len) {
    pk_cheats_clear(core);
    if (!E.loaded || E.gb || !text || !len) return 0;
    // "# name" starts a set, "!disabled" before its codes switches it off, other "!" lines are mGBA's hints; the rest
    // are code lines, which armGBA tells apart itself (GameShark, Action Replay, CodeBreaker).
    char* codes = malloc(len + 2);
    if (!codes) return -1;
    size_t used = 0;
    int sets = 0;
    bool enabled = true, any = false;
    const char* at = text;
    const char* end = text + len;
    while (at <= end) {
        const char* eol = at < end ? memchr(at, '\n', (size_t) (end - at)) : NULL;
        if (!eol) eol = end;
        size_t n = (size_t) (eol - at);
        while (n && (at[n - 1] == '\r' || at[n - 1] == ' ')) n--;
        bool header = n && at[0] == '#', last = at >= end;
        if (header || last) {
            if (used && enabled) {
                codes[used] = 0;
                retro_cheat_set((unsigned) sets++, true, codes);
            }
            used = 0;
            enabled = true;
            any = false;
        } else if (n && at[0] == '!') {
            if (!any && n >= 9 && !strncasecmp(at, "!disabled", 9)) enabled = false;
        } else if (n) {
            if (used) codes[used++] = '+';
            memcpy(codes + used, at, n);
            used += n;
            any = true;
        }
        if (last) break;
        at = eol + 1;
        if (at > end) at = end;
    }
    free(codes);
    return sets;
}

// --- pk_gb_field.h ---

bool pk_gb_write(struct mCore* core, uint32_t addr, const uint8_t* src, int len) {
    if (!core || !E.loaded || !E.gb || len <= 0) return false;
    if (addr >= 0x10000u) {
        uint32_t bank = addr >> 16, off = addr & 0xFFFFu;
        if (bank > 7 || off < 0xD000u || off + (uint32_t) len > 0xE000u || !E.gbView.memory.wram) return false;
        memcpy(E.gbView.memory.wram + bank * 0x1000 + (off - 0xD000u), src, (size_t) len);
        return true;
    }
    if (addr < 0xC000u || addr + (uint32_t) len > 0xE000u) return false;
    for (int i = 0; i < len; i++) core_write8(core, addr + (uint32_t) i, src[i]);
    return true;
}

// Yellow's PC from anywhere calls into the game's code from its overworld loop, which on mGBA stepped the SM83 an
// instruction at a time. Gambatte has no such step, so the call isn't made: the host says "can't do that now".
bool pk_gb_call_at(struct mCore* core, uint16_t at, uint16_t fn, uint8_t a, uint8_t b, uint16_t hl, int maxHalts) {
    (void) core; (void) at; (void) fn; (void) a; (void) b; (void) hl; (void) maxHalts;
    return false;
}
