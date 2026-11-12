// What the 3DS itself gives the app besides the screens: its log, its model, its memory - and what armGBA's own 3DS
// host code (third_party/armgba/platform/3ds/source/jitmem.c, built as it is) asks its frontend for. GPL-3.0 (see NOTICE).
#ifndef PD_CTR_HOST_H
#define PD_CTR_HOST_H

#include <stdbool.h>
#include <stddef.h>

// sdmc:/pokedaisy/log.txt, the last run's kept as log-prev.txt: what to attach to a bug report.
void host_log_open(const char* path);
void host_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void host_log_close(void);
#ifdef PD_LOG_URL
// A diagnosis build only (make LOG_URL=http://<host>:<port>/log, never a release): sends the file there, named `name`.
void host_log_upload(const char* path, const char* name);
#endif
// pd_emu_host.log.
void host_emu_log(void* ctx, const char* line);

// Azahar and Citra (they answer a system info query a console refuses).
bool host_is_emulator(void);
bool host_is_new_3ds(void);
size_t host_heap_size(void);
size_t host_heap_free(void);

#endif
