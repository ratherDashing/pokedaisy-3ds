// autotest.h. GPL-3.0 (see NOTICE).
#ifdef PD_AUTOTEST
#include "autotest.h"

#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "host.h"

static FILE* script;
static char shotDir[128];
static int waitLoops;
static uint32_t keys;
static int keyLoops;
static int touchX, touchY, touchLoops;
static char launch[256];
static bool leaveWanted, quitWanted;
static unsigned loops;

void autotest_start(const char* scriptPath, const char* shots) {
    script = fopen(scriptPath, "r");
    snprintf(shotDir, sizeof(shotDir), "%s", shots);
    host_log(script ? "autotest: running %s" : "autotest: no %s", scriptPath);
}

static uint32_t parse_keys(char* text) {
    static const struct { const char* name; uint32_t bit; } KEYS[] = {
        { "a", KEY_A }, { "b", KEY_B }, { "x", KEY_X }, { "y", KEY_Y }, { "start", KEY_START }, { "select", KEY_SELECT },
        { "l", KEY_L }, { "r", KEY_R }, { "zl", KEY_ZL }, { "zr", KEY_ZR },
        { "up", KEY_DUP }, { "down", KEY_DDOWN }, { "left", KEY_DLEFT }, { "right", KEY_DRIGHT },
    };
    uint32_t bits = 0;
    for (char* k = strtok(text, "+"); k; k = strtok(NULL, "+"))
        for (size_t i = 0; i < sizeof(KEYS) / sizeof(*KEYS); i++)
            if (!strcasecmp(k, KEYS[i].name)) bits |= KEYS[i].bit;
    return bits;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24);
}

// The screens as they are shown (the buffers the display isn't being given next): the hardware keeps each a column at
// a time, bottom up, blue first.
static void shot(const char* name) {
    enum { W = 400, H = 480 };
    char path[300];
    snprintf(path, sizeof(path), "%s/%s.bmp", shotDir, name);
    uint8_t* rows = calloc(1, W * H * 3);
    FILE* f = rows ? fopen(path, "wb") : NULL;
    if (!f) {
        free(rows);
        host_log("autotest: can't write %s", path);
        return;
    }
    u16 w, h;
    u8* top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
    GSPGPU_InvalidateDataCache(top, 400 * 240 * 3);
    u8* bottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    GSPGPU_InvalidateDataCache(bottom, 320 * 240 * 3);
    // A BMP's rows run bottom up: the bottom screen's first.
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) memcpy(rows + ((239 - y) * W + 40 + x) * 3, bottom + (x * 240 + (239 - y)) * 3, 3);
        for (int x = 0; x < 400; x++) memcpy(rows + ((479 - y) * W + x) * 3, top + (x * 240 + (239 - y)) * 3, 3);
    }
    uint8_t head[54] = { 'B', 'M' };
    put32(head + 2, 54 + W * H * 3);
    put32(head + 10, 54);
    put32(head + 14, 40);
    put32(head + 18, W);
    put32(head + 22, H);
    head[26] = 1;
    head[28] = 24;
    put32(head + 34, W * H * 3);
    fwrite(head, 1, sizeof(head), f);
    fwrite(rows, 1, W * H * 3, f);
    fclose(f);
    free(rows);
    host_log("autotest: shot %s at loop %u", name, loops);
}

void autotest_after_frame(void) {
    loops++;
    if (keyLoops > 0 && --keyLoops == 0) keys = 0;
    if (touchLoops > 0) touchLoops--;
    if (!script || quitWanted) return;
    if (waitLoops > 0) {
        waitLoops--;
        return;
    }
    char line[300];
    while (fgets(line, sizeof(line), script)) {
        line[strcspn(line, "\r\n")] = 0;
        char cmd[16] = "";
        int n = 0;
        if (sscanf(line, "%15s %n", cmd, &n) < 1 || cmd[0] == '#') continue;
        char* args = line + n;
        if (!strcmp(cmd, "wait")) {
            waitLoops = atoi(args);
            return;
        } else if (!strcmp(cmd, "press")) {
            char names[64] = "";
            int held = 4;
            sscanf(args, "%63s %d", names, &held);
            keys = parse_keys(names);
            keyLoops = held;
            waitLoops = held + 2;
            return;
        } else if (!strcmp(cmd, "tap")) {
            int held = 4;
            if (sscanf(args, "%d %d %d", &touchX, &touchY, &held) >= 2) {
                touchLoops = held;
                waitLoops = held + 2;
                return;
            }
        } else if (!strcmp(cmd, "launch")) {
            snprintf(launch, sizeof(launch), "%s", args);
            waitLoops = 2;
            return;
        } else if (!strcmp(cmd, "shot")) {
            shot(args);
        } else if (!strcmp(cmd, "log")) {
            host_log("autotest: %s", args);
        } else if (!strcmp(cmd, "leave")) {
            leaveWanted = true;
            waitLoops = 2;
            return;
        } else if (!strcmp(cmd, "quit")) {
            break;
        }
    }
    // The script's end, or quit.
    fclose(script);
    script = NULL;
    quitWanted = true;
    host_log("autotest done");
}

uint32_t autotest_keys(void) {
    return keys;
}

bool autotest_touch(int* x, int* y) {
    if (touchLoops <= 0) return false;
    *x = touchX;
    *y = touchY;
    return true;
}

bool autotest_take_launch(char* out, unsigned outLen) {
    if (!launch[0]) return false;
    snprintf(out, outLen, "%s", launch);
    launch[0] = 0;
    return true;
}

bool autotest_take_leave(void) {
    bool wanted = leaveWanted;
    leaveWanted = false;
    return wanted;
}

bool autotest_quit_wanted(void) {
    return quitWanted;
}
#else
typedef int autotest_not_built;
#endif
