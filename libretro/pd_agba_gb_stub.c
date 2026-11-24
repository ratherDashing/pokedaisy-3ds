/* The Game Boy side of armGBA's libretro glue (libretro/gb.h), without Gambatte.
 *
 * armGBA runs Game Boy and Game Boy Color games on Gambatte, which is GPL-2.0 only and so cannot be linked into this
 * GPL-3.0 core. This core runs Game Boy games on mGBA (pokedaisy_gbc_libretro), so here every Game Boy call says "no":
 * a Game Boy ROM fails to load with a message, and the rest are never reached with a game loaded. GPL-3.0, see ../NOTICE. */
#include "gb.h"

#include <stdio.h>

bool libretroGbLoad(const void* rom, size_t size, const char* systemDirectory, enum LibretroGbModel model, bool colorCorrection,
                    unsigned audioRate, char* error, size_t errorSize) {
  (void)rom; (void)size; (void)systemDirectory; (void)model; (void)colorCorrection; (void)audioRate;
  snprintf(error, errorSize, "This core plays Game Boy Advance games; Game Boy games run on the PokeDaisy core");
  return false;
}
void libretroGbUnload(void) {}
void libretroGbSetBootRom(enum LibretroGbBoot boot) { (void)boot; }
void libretroGbReset(enum LibretroGbModel model) { (void)model; }
void libretroGbRestart(void) {}
void libretroGbSetColorCorrection(bool on) { (void)on; }
void libretroGbSetColorPanel(bool color) { (void)color; }
void libretroGbSetScreen(int screen) { (void)screen; }
void libretroGbSetContrast(int contrast) { (void)contrast; }
uint32_t libretroGbShade(int shade) { (void)shade; return 0; }
uint32_t libretroGbOffColor(void) { return 0; }
uint32_t libretroGbDarkestColor(void) { return 0; }
bool libretroGbOnAdvance(void) { return false; }
uint32_t libretroGbBootCrc(void) { return 0; }
const uint32_t* libretroGbRunFrame(uint16_t keys, const int16_t** samples, size_t* frames) {
  (void)keys;
  *samples = NULL;
  *frames = 0;
  return NULL;
}
void* libretroGbSaveRam(size_t* size) { *size = 0; return NULL; }
void* libretroGbRtc(size_t* size) { *size = 0; return NULL; }
enum LibretroGbClock libretroGbClock(void) { return LIBRETRO_GB_NO_CLOCK; }
void libretroGbSetClock(bool gameTime, unsigned percent, long long offset) { (void)gameTime; (void)percent; (void)offset; }
void libretroGbDeviceTime(bool on) { (void)on; }
bool libretroGbResetClock(void) { return false; }
void libretroGbSetTilt(int x, int y) { (void)x; (void)y; }
void libretroGbFlick(void) {}
void* libretroGbSystemRam(size_t* size) { *size = 0; return NULL; }
size_t libretroGbMemoryMap(struct retro_memory_descriptor* map) { (void)map; return 0; }
bool libretroGbIsColor(void) { return false; }
bool libretroGbSetCheats(const char* const* codes, size_t count) { (void)codes; (void)count; return false; }
void libretroGbSetCpuClock(unsigned quarters, bool loaded) { (void)quarters; (void)loaded; }
bool libretroGbBooting(void) { return false; }
size_t libretroGbStateSize(void) { return 0; }
void libretroGbExactStates(bool exact) { (void)exact; }
bool libretroGbSaveState(void* data, size_t size) { (void)data; (void)size; return false; }
bool libretroGbLoadState(const void* data, size_t size) { (void)data; (void)size; return false; }
int libretroGbLinkOut(void) { return 0; }
void libretroGbLinkIn(int byte) { (void)byte; }
int libretroGbLinkControl(void) { return 0; }
void libretroGbSetPrinter(bool on, void (*output)(const uint8_t* pixels, int rows, bool cut, uint32_t number)) {
  (void)on; (void)output;
}
void libretroGbSelect(int which) { (void)which; }
void libretroGbSetPlayers(int count, bool adapter) { (void)count; (void)adapter; }
void libretroGbRunPlayers(const uint16_t keys[], const uint32_t* pictures[], const int16_t* samples[], size_t frames[]) {
  (void)keys; (void)pictures; (void)samples; (void)frames;
}
/* armGBA 2.0's additions: the output capacitors, the Mega Duck, Datel's Action Replay for the Game Boy, Layer and Channel
 * Selection, netplay's states. */
void libretroGbSetAudioFilter(enum LibretroGbFilter filter) { (void)filter; }
void libretroGbSetAdvanceOutput(enum LibretroAdvanceOutput output) { (void)output; }
void libretroGbSetMegaDuck(int kind) { (void)kind; }
void libretroGbSetMegaDuckLaptop(bool on, int printer, int language) { (void)on; (void)printer; (void)language; }
void libretroGbSetMegaDuckSlot(const void* rom, size_t size, int kind) { (void)rom; (void)size; (void)kind; }
void libretroGbSetActionReplay(const void* program, size_t size, bool game) { (void)program; (void)size; (void)game; }
bool libretroGbSwapGame(const void* rom, size_t size, bool game, char* error, size_t errorSize) {
  (void)rom; (void)size; (void)game;
  if (error && errorSize) snprintf(error, errorSize, "This core plays Game Boy Advance games; Game Boy games run on the PokeDaisy core");
  return false;
}
void libretroGbSetGameGenieCodes(const uint8_t digits[27], const uint8_t counts[3]) { (void)digits; (void)counts; }
void libretroGbActionReplayControls(bool button, bool switchOn) { (void)button; (void)switchOn; }
uint8_t* libretroGbActionReplayFlash(size_t* size) {
  if (size) *size = 0;
  return NULL;
}
bool libretroGbActionReplayFlashChanged(void) { return false; }
void libretroGbKeyboard(bool down, unsigned keycode, uint32_t character, uint16_t modifiers) {
  (void)down; (void)keycode; (void)character; (void)modifiers;
}
const int16_t* libretroGbMixSpeech(const int16_t* samples, size_t frames) { (void)frames; return samples; }
void libretroGbSetLayersShown(unsigned mask) { (void)mask; }
void libretroGbSetMutedChannels(unsigned mask) { (void)mask; }
void libretroGbNetplayStates(bool netplay) { (void)netplay; }
