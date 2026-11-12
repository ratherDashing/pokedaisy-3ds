// PokeDaisy for the Nintendo 3DS: the game on the top screen (armGBA, and Gambatte for the Game Boy: ../emu), the
// companion (../core) on the bottom one. A homebrew .3dsx for the New 3DS; see README.md.
//
// The screens are drawn by the GPU (gpu.c). Audio is ndsp, stereo PCM16, played at the rate the screen paces the game to.
#include <3ds.h>

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

// The port's own seam over armGBA and Gambatte, in mGBA's names (../emu/include): not mGBA's header.
#include <mgba/core/core.h>

#include "../core/pd_canvas.h"
#include "../core/pd_game.h"
#include "../core/pd_games.h"
#include "../core/pd_library.h"
#include "../core/pd_menu.h"
#include "../core/pd_settings.h"
#include "../core/pd_style.h"
#include "../emu/pd_emu.h"
#include "../host/pd_session.h"
#include "host.h"
#include "../../app/src/main/cpp/pk_host.h"
#include "devlink.h"
#include "../../app/src/main/cpp/pk_call.h"
#include "gpu.h"
#include "../../app/src/main/cpp/pk_wide.h"

// The dev build keeps to a folder of its own: a test never touches the player's games and saves.
#ifdef PD_DEV
#define BASE_DIR "/pokedaisy-dev"
#else
#define BASE_DIR "/pokedaisy"
#endif
#define ROM_DIR BASE_DIR "/roms"
#define BACKUP_DIR ROM_DIR "/pokedaisy-backups"
#define STATE_DIR BASE_DIR "/states"
// An optional gba_bios.bin (else armGBA's own BIOS), and what armGBA keeps for itself: each game's warm list.
#define SYSTEM_DIR BASE_DIR "/system"
#define WORK_DIR BASE_DIR "/armgba"
#define SETTINGS_FILE "sdmc:" BASE_DIR "/settings.ini"
#define MAX_BACKUPS 10
#define MAX_ROMS 256

#define TOP_W 400
#define TOP_H 240

// GBA key bits (GBA_KEY_*), as in the app's pokedaisy_jni.c.
enum {
    GBA_A = 1 << 0, GBA_B = 1 << 1, GBA_SELECT = 1 << 2, GBA_START = 1 << 3,
    GBA_RIGHT = 1 << 4, GBA_LEFT = 1 << 5, GBA_UP = 1 << 6, GBA_DOWN = 1 << 7,
    GBA_R = 1 << 8, GBA_L = 1 << 9,
};


static uint32_t topPx[TOP_W * TOP_H];
static uint32_t bottomPx[PD_UI_WIDTH * PD_UI_HEIGHT];
static struct pd_settings settings;
#ifdef PD_AUTOTEST
extern volatile int host_stage;
extern volatile unsigned host_loops;
void host_watch_start(void);
// Where the game's loop is: 1 running the session's tick, 2 after it, 3 drawing, 4 drawn; 0 no game.
#define STAGE(n) (host_stage = (n), host_loops++)
#else
#define STAGE(n) ((void) 0)
#endif

// --- audio ---

// armGBA and Gambatte make 32768 samples for each second of the game's own time, and the game runs a frame to a screen
// refresh: its 59.7275 frames in the 59.8314 the top screen shows. So the channel plays that much faster, as
// armGBA-3DS's own does, and the DSP's resampler (linear) takes it to its own 32728 Hz.
#define SCREEN_HZ (268111856.0 / 4481136.0)
#define DSP_RATE ((float) (PD_EMU_SAMPLE_RATE * SCREEN_HZ / PD_EMU_FPS))
#define AUDIO_SAMPLES 384
#define DSP_BUFFERS 8
// How many chunks the queue keeps ahead, and how far either way it may wander before the rate moves.
#define AUDIO_TARGET 5
#define AUDIO_SLACK 2
#define AUDIO_MADE_EASE 0.02f  // about 0.6 s of chunks
#define AUDIO_RATE_EASE 0.1f
#define AUDIO_QUEUE_PULL 0.004f
#define AUDIO_RATE_MIN 0.85f
#define AUDIO_RATE_MAX 1.01f

static bool hasSound;
static bool audioMuted; // while fast-forwarding
static ndspWaveBuf dspBuffer[DSP_BUFFERS];
static int16_t* audioBuf;
static int bufferId;
static float audioRate = 1.0f, audioRateSet = 1.0f, audioMade = 1.0f;
static u64 audioLast;
static unsigned audioGaps, audioDrops; // the queue ran dry / had no room, for the dev build's stat

#ifdef PD_LOG_URL
// A diagnosis build (make LOG_URL=..., never a release): each second's timing kept in memory, so that writing it
// can't disturb what it measures, and put in the log when the game closes; host.c sends the log to LOG_URL.
static char diagText[128 * 1024];
static size_t diagLen;
static struct {
    unsigned passes, late, none, twice, queueMin;
    u64 worstPass, core, worstCore, ui, worstUi, compiled, warmed;
} diag = { .queueMin = 99 };
static u64 diagLastTop;
#endif

// A game under full speed makes its sound too slowly, and a dry queue is a hole in the music, several a second: that was
// the crunch. So the channel follows how fast the chunks really come: the sound plays as slowly as the game runs, a slow
// glide rather than a gap, and at full speed the rate is exactly 1 again.
static void audio_follow(int queued) {
    u64 now = svcGetSystemTick();
    if (audioLast && now > audioLast) {
        float made = (float) AUDIO_SAMPLES * (float) SYSCLOCK_ARM11 / (float) (now - audioLast) / DSP_RATE;
        if (made < 0.3f) made = 0.3f; // a pause (the HOME menu) says nothing about the game's speed
        if (made > 2.0f) made = 2.0f;
        audioMade += (made - audioMade) * AUDIO_MADE_EASE;
    }
    audioLast = now;
    int off = queued - AUDIO_TARGET;
    float want = audioMade + AUDIO_QUEUE_PULL * (float) off;
    if (want > 0.985f && off >= -AUDIO_SLACK && off <= AUDIO_SLACK) want = 1.0f;
    if (want < AUDIO_RATE_MIN) want = AUDIO_RATE_MIN;
    if (want > AUDIO_RATE_MAX) want = AUDIO_RATE_MAX;
    audioRate += (want - audioRate) * AUDIO_RATE_EASE;
    if (want == 1.0f && audioRate > 0.999f) audioRate = 1.0f;
    float moved = audioRate - audioRateSet;
    if (moved > 0.0005f || moved < -0.0005f || (audioRate == 1.0f && moved != 0.0f)) {
        ndspChnSetRate(0, DSP_RATE * audioRate);
        audioRateSet = audioRate;
    }
}

// The next free chunk, its queue followed; NULL when every one is still queued.
static ndspWaveBuf* audio_next(void) {
    int queued = 0, free = -1;
    for (int i = 0; i < DSP_BUFFERS; i++) {
        int at = (bufferId + i) & (DSP_BUFFERS - 1);
        if (dspBuffer[at].status == NDSP_WBUF_QUEUED || dspBuffer[at].status == NDSP_WBUF_PLAYING) queued++;
        else if (free < 0) free = at;
    }
    if (free < 0) {
        audioDrops++;
        audio_follow(queued);
        return NULL;
    }
    if (!queued) audioGaps++;
#ifdef PD_LOG_URL
    if ((unsigned) queued < diag.queueMin) diag.queueMin = (unsigned) queued;
#endif
    audio_follow(queued);
    bufferId = free;
    ndspWaveBuf* b = &dspBuffer[free];
    int16_t* data = b->data_pcm16;
    memset(b, 0, sizeof(*b));
    b->data_pcm16 = data;
    b->nsamples = AUDIO_SAMPLES;
    return b;
}

static void audio_send(ndspWaveBuf* b) {
    DSP_FlushDataCache(b->data_pcm16, AUDIO_SAMPLES * 2 * sizeof(int16_t));
    ndspChnWaveBufAdd(0, b);
}

// The core's sound as each frame makes it (pd_emu_host.audio), cut into the DSP's chunks; a chunk with nowhere to go
// (muted in fast-forward, no sound, or every buffer still queued) is dropped rather than waited for.
static void emu_audio(void* ctx, const int16_t* stereo, size_t frames) {
    (void) ctx;
    static int16_t chunk[AUDIO_SAMPLES * 2];
    static size_t have;
    while (frames) {
        size_t n = AUDIO_SAMPLES - have < frames ? AUDIO_SAMPLES - have : frames;
        memcpy(chunk + have * 2, stereo, n * 2 * sizeof(int16_t));
        have += n;
        stereo += n * 2;
        frames -= n;
        if (have < AUDIO_SAMPLES) break;
        have = 0;
        ndspWaveBuf* b = hasSound && !audioMuted ? audio_next() : NULL;
        if (!b) continue;
        memcpy(b->data_pcm16, chunk, sizeof(chunk));
        audio_send(b);
    }
}

static void audio_init(void) {
    // ndsp needs the DSP firmware dump (sdmc:/3ds/dspfirm.cdc); without it the
    // game just runs silent. An emulator does the DSP's work itself and has no firmware to give, but ndsp won't start
    // without one: there, and never on a console, it gets a blank one (armGBA-3DS's way, from MW3DS's test setup).
    Result rc = ndspInit();
    if (R_FAILED(rc) && host_is_emulator()) {
        static uint8_t blankFirmware[4096];
        ndspUseComponent(blankFirmware, sizeof(blankFirmware), 0xFF, 0xFF);
        rc = ndspInit();
    }
    if (R_FAILED(rc)) {
        host_log("audio: ndspInit %08lX: no sdmc:/3ds/dspfirm.cdc? The game runs silent", (unsigned long) rc);
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetOutputCount(1);
    ndspChnReset(0);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    // One channel at full level can't clip, and the default soft clipping bends the loud passages.
    ndspSetClippingMode(NDSP_CLIP_NORMAL);
    // At the DSP's rate a sample stays a sample; linear only matters while the rate follows a slow game.
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, DSP_RATE);
    ndspChnWaveBufClear(0);
    audioBuf = linearMemAlign(AUDIO_SAMPLES * DSP_BUFFERS * 2 * sizeof(int16_t), 0x80);
    if (!audioBuf) {
        ndspExit();
        return;
    }
    memset(dspBuffer, 0, sizeof(dspBuffer));
    for (int i = 0; i < DSP_BUFFERS; i++) {
        dspBuffer[i].data_pcm16 = &audioBuf[AUDIO_SAMPLES * i * 2];
        dspBuffer[i].nsamples = AUDIO_SAMPLES;
    }
    hasSound = true;
    host_log("audio: ndsp up, %d-sample chunks at %.1f Hz", AUDIO_SAMPLES, (double) DSP_RATE);
}

static void audio_exit(void) {
    if (!hasSound) return;
    ndspChnWaveBufClear(0);
    ndspExit();
    linearFree(audioBuf);
    hasSound = false;
}

// --- files ---

static void make_dirs(void) {
    mkdir("sdmc:" BASE_DIR, 0777);
    mkdir("sdmc:" ROM_DIR, 0777);
    mkdir("sdmc:" BACKUP_DIR, 0777);
    mkdir("sdmc:" STATE_DIR, 0777);
    mkdir("sdmc:" SYSTEM_DIR, 0777);
    mkdir("sdmc:" WORK_DIR, 0777);
}

// What the app plays: a GBA game, or a Game Boy / Color one.
static bool ends_with_gba(const char* name) {
    size_t gbn = strlen(name);
    if (gbn > 3 && !strcasecmp(name + gbn - 3, ".gb")) return true;
    if (gbn > 4 && !strcasecmp(name + gbn - 4, ".gbc")) return true;
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".gba");
}

static int cmp_str(const void* a, const void* b) {
    return strcasecmp(*(const char* const*) a, *(const char* const*) b);
}

// The .gba files in ROM_DIR, sorted; each entry is malloc'd.
static int list_roms(char** out) {
    int n = 0;
    DIR* d = opendir("sdmc:" ROM_DIR);
    if (!d) return 0;
    struct dirent* e;
    while ((e = readdir(d)) && n < MAX_ROMS) {
        if (e->d_name[0] != '.' && ends_with_gba(e->d_name)) out[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(out, (size_t) n, sizeof(char*), cmp_str);
    return n;
}

static bool copy_file(const char* from, const char* to) {
    FILE* in = fopen(from, "rb");
    if (!in) return false;
    FILE* out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    static char buf[16 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

// Whether name is one of base's backups: exactly "<base>-YYYYmmdd-HHMMSS.sav"
// (a prefix match would count "<base>-2"'s backups as base's and prune them).
static bool is_backup_of(const char* name, const char* base) {
    size_t baseLen = strlen(base);
    if (strncmp(name, base, baseLen)) return false;
    const char* rest = name + baseLen;
    static const char PATTERN[] = "-########-######.sav";
    if (strlen(rest) != sizeof(PATTERN) - 1) return false;
    for (size_t i = 0; PATTERN[i]; i++) {
        if (PATTERN[i] == '#' ? (rest[i] < '0' || rest[i] > '9') : rest[i] != PATTERN[i]) return false;
    }
    return true;
}

// Save files are sacred (CLAUDE.md): every start - and every state load -
// copies the save into BACKUP_DIR first, keeping the newest MAX_BACKUPS per game.
static void backup_save(const char* base, const char* savePath) {
    struct stat st;
    if (stat(savePath, &st) || st.st_size == 0) return;
    char stamp[32];
    time_t now = time(NULL);
    // The 3DS clock is local time; gmtime keeps it as it is.
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", gmtime(&now));
    char to[512];
    snprintf(to, sizeof(to), "sdmc:" BACKUP_DIR "/%s-%s.sav", base, stamp);
    copy_file(savePath, to);

    // Prune: the names sort by time, so the oldest come first.
    char* names[64];
    int n = 0;
    DIR* d = opendir("sdmc:" BACKUP_DIR);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && n < 64) {
        if (is_backup_of(e->d_name, base)) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t) n, sizeof(char*), cmp_str);
    for (int i = 0; i < n; i++) {
        if (i < n - MAX_BACKUPS) {
            char p[512];
            snprintf(p, sizeof(p), "sdmc:" BACKUP_DIR "/%s", names[i]);
            remove(p);
        }
        free(names[i]);
    }
}

// --- the game ---

static uint32_t gba_keys(u32 held) {
    uint32_t k = 0;
    if (held & KEY_A) k |= GBA_A;
    if (held & KEY_B) k |= GBA_B;
    if (held & KEY_SELECT) k |= GBA_SELECT;
    if (held & KEY_START) k |= GBA_START;
    if (held & KEY_RIGHT) k |= GBA_RIGHT; // KEY_RIGHT = D-pad or circle pad
    if (held & KEY_LEFT) k |= GBA_LEFT;
    if (held & KEY_UP) k |= GBA_UP;
    if (held & KEY_DOWN) k |= GBA_DOWN;
    if (held & KEY_R) k |= GBA_R;
    if (held & KEY_L) k |= GBA_L;
    return k;
}

static void show_message(const char* line1, const char* line2) {
    struct pd_canvas top;
    pd_canvas_init(&top, TOP_W, TOP_H, topPx);
    pd_splash_draw(&top, line1, line2, "Press B.");
    gpu_set_top(topPx);
    while (aptMainLoop()) {
        hidScanInput();
        if ((hidKeysDown() | devlink_keys()) & (KEY_B | KEY_A) || devlink_restart_wanted()) break;
        gpu_draw(false, 0);
        devlink_after_frame();
    }
}

// The state file's time for LOAD STATE, or false if there's none. stat()
// leaves the time 0 on the SD card; archive_getmtime reads it.
// Where the frame's time goes, in the dev build (devlink.h's stats).
#ifdef PD_DEV
#define DEV_MARK(name) u64 name = svcGetSystemTick()
#define TICKS_TO_US(t) ((unsigned) ((t) / (SYSCLOCK_ARM11 / 1000000)))
#else
#define DEV_MARK(name)
#endif

// The stylus, or the dev link's: true while one is down.
static bool read_touch(u32 held, int* x, int* y) {
    if (devlink_touch(x, y)) return true;
    if (!(held & KEY_TOUCH)) return false;
    touchPosition t;
    hidTouchRead(&t);
    *x = t.px;
    *y = t.py;
    return true;
}

// GAME VOLUME / MUTE GAME: the sound channel's level.
static void apply_volume(void) {
    if (!hasSound) return;
    float v = settings.gameMuted ? 0.0f : settings.gameVolume / 100.0f;
    float mix[12] = { v, v };
    ndspChnSetMix(0, mix);
}

// The left stick for the QUICK MENU's ring: degrees clockwise from up, -1 near the middle.
static int stick_angle(void) {
    circlePosition pad;
    hidCircleRead(&pad);
    if (pad.dx * pad.dx + pad.dy * pad.dy < 80 * 80) return -1;
    float a = atan2f((float) pad.dx, (float) pad.dy) * 180.0f / 3.14159265f;
    return (int) (a < 0 ? a + 360 : a) % 360;
}

struct game_files {
    char base[256];
    char saveSd[520];
};

// Twice the same: mGBA's 3DS VFile took the path without the sdmc: stdio wants.
static void state_path(void* ctx, int slot, char* vf, size_t vfLen, char* path, size_t pathLen) {
    const struct game_files* f = ctx;
    snprintf(path, pathLen, "sdmc:" STATE_DIR "/%s.ss%d", f->base, slot + 1);
    snprintf(vf, vfLen, "%s", path);
}

static void backup_for_load(void* ctx) {
    const struct game_files* f = ctx;
    backup_save(f->base, f->saveSd);
}

static void settings_changed(void* ctx) {
    (void) ctx;
    pd_settings_save(&settings, SETTINGS_FILE);
}

static void cheat_path(void* ctx, uint32_t crc, char* path, size_t len) {
    (void) ctx;
    snprintf(path, len, "sdmc:" BASE_DIR "/cheats/%08lX.cheats", (unsigned long) crc);
}

// Runs one game until the player leaves (true) or the app is closed (false).
static bool run_game(const char* file) {
    char romPath[512], savePath[512], base[256];
    snprintf(romPath, sizeof(romPath), ROM_DIR "/%s", file);
    snprintf(base, sizeof(base), "%s", file);
    char* dot = strrchr(base, '.');
    if (dot) *dot = 0;
    snprintf(savePath, sizeof(savePath), ROM_DIR "/%s.sav", base);
    char saveSd[520];
    snprintf(saveSd, sizeof(saveSd), "sdmc:%s", savePath);

    char romSd[520];
    snprintf(romSd, sizeof(romSd), "sdmc:%s", romPath);
    FILE* rom = fopen(romSd, "rb");
    if (!rom) {
        show_message("Couldn't open the game:", file);
        return true;
    }
    uint8_t header[0xC0] = { 0 };
    size_t headerRead = fread(header, 1, sizeof(header), rom);
    fseek(rom, 0, SEEK_END);
    long romLength = ftell(rom);
    fclose(rom);
    struct pd_game game;
    pd_game_detect(&game, header, headerRead == sizeof(header) && romLength > 0 ? (size_t) romLength : 0);

    // The save as it is, copied aside before anything can touch it (CLAUDE.md: save files are sacred).
    backup_save(base, saveSd);
    audioRate = audioRateSet = audioMade = 1.0f;
    audioLast = 0;
    if (hasSound) ndspChnSetRate(0, DSP_RATE);
    // armGBA: its recompiler, its renderer on the New 3DS's third and fourth cores and its mixer on the fourth, as in
    // armGBA-3DS. Gambatte for a Game Boy game.
    struct pd_emu_host emuHost = { .audio = emu_audio, .log = host_emu_log, .systemDir = "sdmc:" SYSTEM_DIR, .workDir = "sdmc:" WORK_DIR,
                                   .threaded = host_is_new_3ds() };
    char error[96] = "";
    host_log("game: %s (%ld KiB), heap %lu KiB free", file, romLength >> 10, (unsigned long) host_heap_free() >> 10);
    struct mCore* core = pd_emu_load(romSd, saveSd, &emuHost, error, sizeof(error));
    if (!core) {
        host_log("game: didn't load: %s", error);
        show_message("The game didn't start:", error);
        return true;
    }
    core->setVideoBuffer(core, (color_t*) gpu_game_buffer(), GPU_GAME_STRIDE);
    bool gb = core->platform(core) == mPLATFORM_GB;
    gpu_double_game(!gb);
    unsigned videoW = 240, videoH = 160;
    core->desiredVideoDimensions(core, &videoW, &videoH);
    gpu_set_game_size((int) videoW, (int) videoH);
    pk_heal_rtc(core);
    host_log("game: running, %s; heap %lu KiB free", gb ? "Gambatte" : pd_emu_recompiling(core) ? "armGBA's recompiler" : "armGBA's INTERPRETER (no executable memory)",
             (unsigned long) host_heap_free() >> 10);

    static struct pd_session session;
    memset(&session, 0, sizeof(session));
    session.game = game;
    struct game_files gameFiles;
    snprintf(gameFiles.base, sizeof(gameFiles.base), "%s", base);
    snprintf(gameFiles.saveSd, sizeof(gameFiles.saveSd), "%s", saveSd);
    // No thread for EXPAND: it's off in this build, and armGBA's renderer has both spare cores.
    struct pd_session_host files = { state_path, backup_for_load, settings_changed, cheat_path, &gameFiles, NULL, NULL, NULL };
#ifdef PD_DEV
    files.clock = svcGetSystemTick;
#endif
    session.host = (struct pd_host_info) { .count = 2 };
    session.host.labels[0] = "FILE";
    snprintf(session.host.values[0], sizeof(session.host.values[0]), "%s", file);
    session.host.labels[1] = "SPEED";
    snprintf(session.host.values[1], sizeof(session.host.values[1]), "-");
    pd_session_start(&session, core, gpu_game_buffer(), &settings, &files);
    host_log("game: session started (%s)", session.game.cfg ? "supported" : "not a game the companion knows");
    apply_volume();
    struct pd_ui* uiPtr = &session.ui;
    struct pd_host_info* hostPtr = &session.host;
    struct pd_canvas bottom;
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, bottomPx);

    bool touching = false;
    unsigned fpsFrames = 0;
    u64 busyTicks = 0;
    const uint32_t* perfPicture = NULL;
    int perfWidth = 0;
    u64 fpsStart = osGetTime();
    u64 lastTick = fpsStart;
#ifdef PD_DEV
    u64 devSecond = fpsStart;
#endif
    bool appRunning = true;
    u32 lastInjected = 0, withheld = 0;
#ifdef PD_DEV
    struct devlink_stats dev = { 0 };
    u64 loopStart = svcGetSystemTick();
#endif

    // What a pass of this loop takes without compiling, in ticks: the largest of late, forgotten slowly. What is left
    // of a frame's time is what armGBA's recompiler may spend compiling in it; past that it steps through new code and
    // compiles it in a later frame. Without the limit a step into new code (a door, a new map's scripts) compiled it
    // all in one frame, the frame ran long and the sound queue ran dry: a crackle.
    const double framePeriod = (double) SYSCLOCK_ARM11 * 4481136.0 / 268111856.0;
    const double tickMs = (double) SYSCLOCK_ARM11 / 1000.0;
    double typicalWork = framePeriod / 2;

    while ((appRunning = aptMainLoop())) {
        u64 passTop = svcGetSystemTick();
        u64 compiledAtTop = pd_emu_compile_ticks();
        double compileBudget = framePeriod - typicalWork - 1.5 * tickMs; // the 1.5 ms: handing the frame to the GPU
        if (compileBudget < 0.25 * tickMs) compileBudget = 0.25 * tickMs;
        if (compileBudget > 6.0 * tickMs) compileBudget = 6.0 * tickMs;
        hidScanInput();
        u32 injected = devlink_keys();
        u32 held = hidKeysHeld() | injected, down = hidKeysDown() | (injected & ~lastInjected);
        lastInjected = injected;
        if (devlink_take_leave() || devlink_restart_wanted()) break;
#ifdef PD_DEV
        int optScreen, optAspect;
        if (devlink_take_opt(&optScreen, &optAspect)) {
            if (optScreen >= 0) settings.screenMode = optScreen;
            if (optAspect >= 0) settings.aspect = optAspect;
            session.redraw = true;
        }
        int wildSpecies, wildLevel;
        if (session.game.kind != PD_GAME_GB && pk_call_safe(core) && session.game.cfg && session.game.cfg->pcSetupScript && devlink_take_wild(&wildSpecies, &wildLevel)) {
            // setwildbattle species level item; dowildbattle; end (pd_sim's `wild`).
            const struct pd_config* cfg = session.game.cfg;
            uint8_t script[] = { 0xB6, (uint8_t) wildSpecies, (uint8_t) (wildSpecies >> 8), (uint8_t) wildLevel, 0, 0, 0xB7, 0x02 };
            pd_session_write(&session, cfg->pcScript, script, sizeof(script));
            uint32_t args[4] = { cfg->pcScript, 0, 0, 0 };
            pk_call(core, cfg->pcSetupScript, args, NULL);
        }
#endif

        // X on the field sends the buttons to the companion (the app's bottom focus), elsewhere it's BACK; Y the QUICK
        // MENU; ZL the tab before. With the buttons on the companion L / R step the tabs, the D-pad and A its cursor;
        // X, START, or B with nothing left to go back from bring them back.
        if (uiPtr->keyFocus) {
            static const u32 DIRS[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
            static int heldFor[4];
            if (down & (KEY_X | KEY_START)) pd_session_set_focus(&session, false);
            else if ((down & KEY_B) && !pd_ui_back(uiPtr)) pd_session_set_focus(&session, false);
            else if (down & KEY_A) pd_ui_focus_press(uiPtr);
            else if (down & (KEY_L | KEY_R)) pd_ui_next_tab(uiPtr, down & KEY_L ? -1 : 1);
            for (int i = 0; i < 4; i++) {
                heldFor[i] = held & DIRS[i] ? heldFor[i] + 1 : 0;
                if ((down & DIRS[i]) || (heldFor[i] >= 24 && heldFor[i] % 6 == 0)) {
                    pd_ui_focus_move(uiPtr, i);
                    session.redraw = true;
                }
            }
            if (down) session.redraw = true;
            withheld |= held;
        } else {
            if (down & KEY_X) {
                if (pd_session_can_focus(&session)) {
                    pd_session_set_focus(&session, true);
                    withheld |= held;
                } else {
                    pd_ui_back(uiPtr);
                }
                session.redraw = true;
            }
            if (down & KEY_ZL) { pd_ui_next_tab(uiPtr, -1); session.redraw = true; }
        }
        // Keys pressed on the companion stay away from the game until they're let go.
        withheld &= held;

        int touchX, touchY;
        if (read_touch(held, &touchX, &touchY)) {
            session.redraw |= pd_ui_touch(uiPtr, touchX, touchY, true);
            touching = true;
        } else if (touching) {
            session.redraw |= pd_ui_touch(uiPtr, 0, 0, false);
            touching = false;
        }

        bool leave = false;
        enum pd_action action = pd_ui_take_action(uiPtr);
        if (!pd_session_action(&session, action)) {
            switch (action) {
            case PD_ACTION_LEAVE_GAME:
                leave = true;
                break;
            case PD_ACTION_RESTART_GAME:
                pk_finish_save(core, PK_FINISH_SAVE_FRAMES, NULL);
                core->reset(core);
                pk_heal_rtc(core);
                break;
            case PD_ACTION_SETTINGS_CHANGED:
                pd_settings_save(&settings, SETTINGS_FILE);
                apply_volume();
                break;
            default:
                break;
            }
        }
        if (leave) break;

        struct pd_input in = { .held = gba_keys(held & ~withheld), .down = gba_keys(down & ~withheld),
                               .quickMenu = (down & KEY_Y) && !uiPtr->keyFocus,
                               .ffHeld = held & KEY_ZR, .stickAngle = stick_angle() };
        pd_emu_compile_budget((held & KEY_ZR) || session.ff ? ~0ull : (uint64_t) compileBudget);
        DEV_MARK(coreStart);
        u64 busyStart = svcGetSystemTick();
        u64 tickNow = osGetTime();
        unsigned before = session.frame;
        STAGE(1);
        pd_session_tick(&session, &in, (unsigned) (tickNow - lastTick));
        STAGE(2);
        lastTick = tickNow;
        unsigned frames = session.frame - before;
        if (frames) pd_session_set_video(&session, gpu_frame_done());
        audioMuted = session.ff;
        busyTicks += svcGetSystemTick() - busyStart;
#ifdef PD_LOG_URL
        u64 diagCoreEnd = svcGetSystemTick();
        {
            u64 took = diagCoreEnd - busyStart;
            diag.core += took;
            if (took > diag.worstCore) diag.worstCore = took;
            diag.passes++;
            if (session.frame == before) diag.none++;
            if (session.frame - before >= 2) diag.twice++;
            if (diagLastTop) {
                u64 pass = passTop - diagLastTop;
                if (pass > diag.worstPass) diag.worstPass = pass;
                if (pass > (u64) (20.0 * tickMs)) diag.late++;
            }
            diagLastTop = passTop;
        }
#endif
        DEV_MARK(uiStart);

        fpsFrames += (unsigned) frames;
        u64 now = osGetTime();
        if (now - fpsStart >= 1000) {
            // Against the GBA's own 268111856 / 4481136 = 59.73 frames a second.
            unsigned pct = (unsigned) ((u64) fpsFrames * 1000 * 100 * 4481136 / ((now - fpsStart) * 268111856ULL));
            snprintf(hostPtr->values[1], sizeof(hostPtr->values[1]), "%u%% (%u FPS)", pct, fpsFrames);
            // The game's own core, how much of the last second it was busy: 100 and the game can't keep up.
            unsigned cpu = (unsigned) (busyTicks / (SYSCLOCK_ARM11 / 1000) * 100 / (now - fpsStart));
            perfPicture = settings.showPerf ? pd_session_perf((unsigned) ((u64) fpsFrames * 10000 / (now - fpsStart)), cpu > 100 ? 100 : cpu, &perfWidth) : NULL;
            // The test build logs every second; a player's build only a second whose sound broke, for a report.
#ifndef PD_AUTOTEST
            if (audioGaps || audioDrops)
#endif
                host_log("speed: %u frames in %u ms, core busy %u%%, sound gaps %u drops %u rate %u", fpsFrames, (unsigned) (now - fpsStart), cpu,
                         audioGaps, audioDrops, (unsigned) (audioRate * 1000));
#ifdef PD_LOG_URL
            if (diagLen + 200 < sizeof(diagText)) {
                #define MS10(t) ((unsigned) ((t) * 10 / (SYSCLOCK_ARM11 / 1000)))
                diagLen += (size_t) snprintf(diagText + diagLen, sizeof(diagText) - diagLen,
                    "%u f%u gaps%u drops%u rate%u q%u | passes%u late%u none%u twice%u worst%u | core%u worst%u ui%u worst%u compile%u warm%u (0.1 ms)\n",
                    (unsigned) (now / 1000 % 100000), fpsFrames, audioGaps, audioDrops, (unsigned) (audioRate * 1000), diag.queueMin,
                    diag.passes, diag.late, diag.none, diag.twice, MS10(diag.worstPass),
                    diag.passes ? MS10(diag.core / diag.passes) : 0, MS10(diag.worstCore),
                    diag.passes ? MS10(diag.ui / diag.passes) : 0, MS10(diag.worstUi), MS10(diag.compiled), MS10(diag.warmed));
                #undef MS10
            }
            memset(&diag, 0, sizeof(diag));
            diag.queueMin = 99;
#endif
            audioGaps = audioDrops = 0;
            busyTicks = 0;
            fpsFrames = 0;
            fpsStart = now;
            u8 battery = 0;
            if (R_SUCCEEDED(MCUHWC_GetBatteryLevel(&battery))) hostPtr->batteryPct = battery;
            if (uiPtr->tab == PD_TAB_SETTINGS) session.redraw = true;
        }

        if (session.redraw) {
            pd_session_draw(&session, &bottom);
            gpu_set_bottom(bottomPx);
        }
        DEV_MARK(drawStart);
        gpu_set_perf(perfPicture, session.perfRight - perfWidth, 3, settings.showPerf ? perfWidth : 0);
        perfPicture = NULL;
        gpu_hold_game(pd_session_holds_frame(&session));
        gpu_set_wide(session.wideReady ? pd_session_wide(&session) : NULL, session.wideW, session.wideH);
        const struct pd_wide_hole* hole = &session.wideHole;
        gpu_set_wide_hole(hole->x, hole->y, hole->w, hole->h, hole->gameX, hole->gameY);
        if (session.overlayDirty) gpu_set_overlay(pd_session_overlay(&session));
        gpu_show_overlay(session.overlayShown, session.overlayDim);
        {
            u64 nowTicks = svcGetSystemTick();
            double work = (double) (nowTicks - passTop) - (double) (pd_emu_compile_ticks() - compiledAtTop);
            if (frames == 1 && work < 3 * framePeriod) typicalWork = work > typicalWork ? work : typicalWork - (typicalWork - work) / 32;
            // What is left of the frame's time goes to armGBA's warm list (the blocks the game ran last time), a block
            // at a time, so that they are compiled before the game reaches them.
#ifdef PD_LOG_URL
            u64 uiTook = nowTicks - diagCoreEnd;
            diag.ui += uiTook;
            if (uiTook > diag.worstUi) diag.worstUi = uiTook;
            diag.compiled += pd_emu_compile_ticks() - compiledAtTop;
            u64 warmStart = nowTicks;
#endif
            while (!session.ff && (double) (nowTicks - passTop) + 2.0 * tickMs < framePeriod && pd_emu_warm_one()) nowTicks = svcGetSystemTick();
#ifdef PD_LOG_URL
            diag.warmed += nowTicks - warmStart;
#endif
        }
        STAGE(3);
        gpu_draw(true, pd_session_screen(&session));
        STAGE(4);
        devlink_after_frame();
#ifdef PD_DEV
        {
            u64 end = svcGetSystemTick();
            dev.coreUs += TICKS_TO_US(uiStart - coreStart);
            dev.uiUs += TICKS_TO_US(drawStart - uiStart);
            dev.drawUs += TICKS_TO_US(end - drawStart);
            unsigned whole = TICKS_TO_US(end - loopStart);
            if (whole > dev.worstUs) dev.worstUs = whole;
            if (whole > 17500) dev.late++;
            loopStart = end;
            dev.frames += (unsigned) frames;
            if (now - devSecond >= 1000) {
                devSecond = now;
                dev.runUs = TICKS_TO_US(session.prof.run);
                dev.waitUs = TICKS_TO_US(session.prof.wait);
                dev.wideUs = TICKS_TO_US(session.prof.wide);
                dev.snapUs = TICKS_TO_US(session.prof.snap);
                memset(&session.prof, 0, sizeof(session.prof));
                dev.gaps = audioGaps;
                dev.drops = audioDrops;
                dev.rate = (unsigned) (audioRate * 1000);
                audioGaps = audioDrops = 0;
                devlink_report(&dev);
                memset(&dev, 0, sizeof(dev));
            }
        }
#endif
    }

    // A save the game is writing finishes, unheard, before the core stops; then deinit writes the file.
    audioMuted = true;
    pk_finish_save(core, PK_FINISH_SAVE_FRAMES, NULL);
    audioMuted = false;
    if (hasSound) ndspChnWaveBufClear(0);
    // Writes the save if the game changed it, and armGBA's warm list.
    core->deinit(core);
    STAGE(0);
#ifdef PD_LOG_URL
    host_log("diag: one line a second, times in 0.1 ms\n%s", diagText);
    diagLen = 0;
    diagText[0] = 0;
#endif
    host_log("game: closed");
    return appRunning;
}

// --- the game list ---

// Picks a game; false when the app should close.
static bool pick_rom(char* out, size_t outLen) {
    static struct pd_library library;
    static char titles[PD_LIBRARY_MAX][96];
    static char* names[MAX_ROMS];
    static int viewKept;
    int n = list_roms(names);
    pd_library_init(&library);
    library.view = viewKept;
    for (int i = 0; i < n && i < PD_LIBRARY_MAX; i++) {
        struct pd_library_game* g = &library.games[library.count++];
        g->file = names[i];
        g->title = titles[i];
        pd_library_title(names[i], titles[i], sizeof(titles[i]), &g->hack, &g->supported);
        size_t len = strlen(names[i]);
        g->console = len > 4 && !strcasecmp(names[i] + len - 4, ".gbc") ? PD_CONSOLE_GBC
            : len > 3 && !strcasecmp(names[i] + len - 3, ".gb") ? PD_CONSOLE_GB : PD_CONSOLE_GBA;
    }
    pd_look_apply(NULL, &settings);
    pd_library_refresh(&library);
    struct pd_canvas top, bottom;
    pd_canvas_init(&top, TOP_W, TOP_H, topPx);
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, bottomPx);

    bool dirty = true;
    bool touching = false, picked = false, running = true;
    u32 lastInjected = 0;
#ifdef PD_DEV
    char shownAddress[32] = "";
#endif
    while ((running = aptMainLoop())) {
        hidScanInput();
        u32 injected = devlink_keys();
        u32 down = hidKeysDown() | (injected & ~lastInjected), held = hidKeysHeld() | injected;
        lastInjected = injected;
        if (down & KEY_START || devlink_restart_wanted()) {
            running = false;
            break;
        }
        char wanted[256];
        if (devlink_take_launch(wanted, sizeof(wanted))) {
            for (int i = 0; i < library.count; i++)
                if (!strcmp(library.games[i].file, wanted)) library.picked = i;
        }
        int dx = !!(down & KEY_RIGHT) - !!(down & KEY_LEFT), dy = !!(down & KEY_DOWN) - !!(down & KEY_UP);
        if (dx || dy) {
            pd_library_move(&library, dx, dy);
            dirty = true;
        }
        if (down & KEY_L || down & KEY_R) {
            library.view = (library.view + (down & KEY_R ? 1 : PD_VIEW_COUNT - 1)) % PD_VIEW_COUNT;
            library.selected = library.scroll = 0;
            pd_library_refresh(&library);
            dirty = true;
        }
        if ((down & KEY_A) && library.shownCount) library.picked = library.shown[library.selected];
        int touchX, touchY;
        if (read_touch(held, &touchX, &touchY)) {
            pd_library_touch(&library, touchX, touchY, true);
            touching = true;
            dirty = true;
        } else if (touching) {
            touching = false;
            pd_library_touch(&library, 0, 0, false);
            dirty = true;
        }
        if (library.picked >= 0) {
            picked = true;
            break;
        }
        if (dirty) {
            pd_library_draw(&library, &bottom);
            gpu_set_bottom(bottomPx);
#ifdef PD_DEV
            snprintf(shownAddress, sizeof(shownAddress), "%s", devlink_address());
            char line[64];
            snprintf(line, sizeof(line), "Dev link: %s", shownAddress);
            pd_library_draw_top(&library, &top, line);
#else
            pd_library_draw_top(&library, &top, NULL);
#endif
            gpu_set_top(topPx);
            dirty = false;
        }
        gpu_draw(false, 0);
        devlink_after_frame();
#ifdef PD_DEV
        if (strcmp(shownAddress, devlink_address())) dirty = true;
#endif
    }
    viewKept = library.view;
    if (picked) snprintf(out, outLen, "%s", library.games[library.picked].file);
    for (int i = 0; i < n; i++) free(names[i]);
    return running && picked;
}

int main(void) {
    if (!gpu_init()) {
        gfxExit();
        return 1;
    }
    // The New 3DS's 804 MHz mode.
    osSetSpeedupEnable(true);
    make_dirs();
    host_log_open("sdmc:" BASE_DIR "/log.txt");
#ifdef PD_LOG_URL
    host_log_upload("sdmc:" BASE_DIR "/log-prev.txt", "pokedaisy-prev");
#endif
    host_log("PokeDaisy 3DS (armGBA core), %s%s, heap %lu KiB, linear %lu KiB", host_is_new_3ds() ? "New 3DS" : "old 3DS",
             host_is_emulator() ? ", in an emulator" : "", (unsigned long) host_heap_size() >> 10, (unsigned long) linearSpaceFree() >> 10);
    // armGBA's recompiler and its drawing on other cores need the New 3DS: the original models are refused here, not
    // left to run at a fraction of full speed.
    if (!host_is_new_3ds()) {
        show_message("PokeDaisy needs a New 3DS,", "New 3DS XL or New 2DS XL.");
        host_log_close();
        gpu_exit();
        return 0;
    }
    pd_settings_load(&settings, SETTINGS_FILE);
    audio_init();
    mcuHwcInit();
    devlink_start();
#ifdef PD_AUTOTEST
    mkdir("sdmc:" BASE_DIR "/shots", 0777);
    autotest_start("sdmc:" BASE_DIR "/autotest.txt", "sdmc:" BASE_DIR "/shots");
    host_watch_start();
#endif
    char file[256];
    while (pick_rom(file, sizeof(file))) {
        if (!run_game(file)) break;
    }

#ifdef PD_DEV
    bool restart = devlink_restart_wanted();
#endif
    devlink_stop();
#ifdef PD_DEV
    if (restart) devlink_chainload();
#endif
    mcuHwcExit();
    audio_exit();
    host_log("exit");
    host_log_close();
#ifdef PD_LOG_URL
    host_log_upload("sdmc:" BASE_DIR "/log.txt", "pokedaisy");
#endif
    gpu_exit();
    return 0;
}
