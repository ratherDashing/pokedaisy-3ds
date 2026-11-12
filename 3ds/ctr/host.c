// host.h, and the memory split. GPL-3.0 (see NOTICE).
#include "host.h"

#include <3ds.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// armGBA's libretro layer keeps several long paths on the stack.
u32 __stacksize__ = 512 * 1024;

// libctru splits the app's memory evenly between the heap and linear memory (what the GPU and the DSP read), up to
// 32 MiB of linear. Here the heap holds the game (up to 32 MiB), the recompiler's code (up to 24) and the copies of
// the machine its render and sound threads draw from, while the screens' textures, the frames in flight and the sound
// need about 5.5 MiB of linear memory (measured at start): it gets 8, and the heap the rest, as far as the heap's address range goes (96
// MiB; asked for more, the allocation fails and the app stops before main - armGBA-3DS's finding on a New 3DS).
#define LINEAR_WANTED (8u << 20)
extern char* fake_heap_start;
extern char* fake_heap_end;
u32 __ctru_heap;
u32 __ctru_heap_size;
u32 __ctru_linear_heap;
u32 __ctru_linear_heap_size;
void mappableInit(u32 addrMin, u32 addrMax);

void __system_allocateHeaps(void) {
    // What the process may still commit, as libctru's own split asks.
    Handle limit = 0;
    s64 most = 0, now = 0;
    ResourceLimitType commit = RESLIMIT_COMMIT;
    if (R_FAILED(svcGetResourceLimit(&limit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
    svcGetResourceLimitLimitValues(&most, limit, &commit, 1);
    svcGetResourceLimitCurrentValues(&now, limit, &commit, 1);
    svcCloseHandle(limit);
    u32 remaining = (u32) (most - now) & ~0xFFFu;
    u32 area = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
    u32 heap = remaining > LINEAR_WANTED ? remaining - LINEAR_WANTED : remaining / 2;
    if (heap > area) heap = area;
    __ctru_heap_size = heap;
    __ctru_linear_heap_size = remaining - heap;
    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0, __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE)))
        svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0, 0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE)))
        svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*) __ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}

static FILE* logFile;
static u64 logStart;

void host_log_open(const char* path) {
    char prev[300];
    snprintf(prev, sizeof(prev), "%.*s-prev.txt", (int) (strlen(path) > 4 ? strlen(path) - 4 : strlen(path)), path);
    remove(prev);
    rename(path, prev);
    logFile = fopen(path, "w");
    logStart = osGetTime();
}

static void log_va(const char* format, va_list args) {
    if (!logFile) return;
    fprintf(logFile, "[%7.2f] ", (double) (osGetTime() - logStart) / 1000.0);
    vfprintf(logFile, format, args);
    fputc('\n', logFile);
    fflush(logFile); // a crash leaves the lines so far
}

void host_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_va(format, args);
    va_end(args);
}

void host_log_close(void) {
    if (logFile) fclose(logFile);
    logFile = NULL;
}

#ifdef PD_LOG_URL
void* readFile(const char* path, size_t* size);

void host_log_upload(const char* path, const char* name) {
    size_t size = 0;
    char* data = readFile(path, &size);
    if (!data || !size) {
        free(data);
        return;
    }
    // The http service holds a POST's body in memory shared with it.
    if (R_FAILED(httpcInit(1 << 20))) {
        free(data);
        return;
    }
    size_t most = 768 * 1024; // the end, if it's long
    if (size > most) {
        memmove(data, data + size - most, most);
        size = most;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s?name=%s", PD_LOG_URL, name);
    httpcContext context;
    u32 status = 0;
    Result rc = httpcOpenContext(&context, HTTPC_METHOD_POST, url, 0);
    if (R_SUCCEEDED(rc)) {
        httpcAddRequestHeaderField(&context, "Content-Type", "text/plain");
        httpcAddPostDataRaw(&context, (const u32*) data, size);
        rc = httpcBeginRequest(&context);
        if (R_SUCCEEDED(rc)) rc = httpcGetResponseStatusCodeTimeout(&context, &status, 3000000000ull);
        httpcCloseContext(&context);
    }
    host_log("log upload: %s: %08lX, HTTP %lu", name, (unsigned long) rc, (unsigned long) status);
    httpcExit();
    free(data);
}
#endif

void host_emu_log(void* ctx, const char* line) {
    (void) ctx;
    host_log("%s", line);
}

bool host_is_emulator(void) {
    static int known = -1;
    if (known < 0) {
        s64 value = 0;
        known = R_SUCCEEDED(svcGetSystemInfo(&value, 0x20000, 0)) && value != 0;
    }
    return known;
}

bool host_is_new_3ds(void) {
    bool isNew = false;
    APT_CheckNew3DS(&isNew);
    return isNew;
}

size_t host_heap_size(void) {
    return __ctru_heap_size;
}

size_t host_heap_free(void) {
    struct mallinfo m = mallinfo();
    return __ctru_heap_size > (u32) m.uordblks ? __ctru_heap_size - (u32) m.uordblks : 0;
}

// --- what armGBA's 3DS host code (jitmem.c: executable memory, the render and sound threads, the warm list) calls
// in its own frontend, under the names it calls them by ---

void logLine(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_va(format, args);
    va_end(args);
}

bool isEmulator(void) {
    return host_is_emulator();
}

bool isNew3ds(void) {
    return host_is_new_3ds();
}

bool writeFile(const char* path, const void* data, size_t size) {
    char temp[600];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE* f = fopen(temp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        remove(temp);
        return false;
    }
    remove(path);
    return rename(temp, path) == 0;
}

void* readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = length >= 0 ? malloc(length ? (size_t) length : 1) : NULL;
    if (data && fread(data, 1, (size_t) length, f) != (size_t) length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (size_t) length;
    return data;
}

#ifdef PD_AUTOTEST
// The test build's watchdog: once a second, where the main loop is, in case it stops.
volatile int host_stage;
extern volatile int pd_emu_stage;
volatile unsigned host_loops;
static void watch_main(void* arg) {
    (void) arg;
    unsigned last = ~0u;
    int same = 0;
    for (;;) {
        svcSleepThread(1000000000LL);
        if (host_stage && host_loops == last) { // stage 0: no game running
            if (++same <= 5 || same % 30 == 0) host_log("watchdog: the main loop has stood at stage %d / %d for %d s (loop %u)", host_stage, pd_emu_stage, same, last);
        } else {
            same = 0;
        }
        last = host_loops;
    }
}
void host_watch_start(void) {
    threadCreate(watch_main, NULL, 16 * 1024, 0x19, 1, true);
}
#endif
