// A test build's script (PD_AUTOTEST, never a player's build): sdmc:/pokedaisy/autotest.txt drives the app with no
// hands - in the Azahar emulator, where nothing can press its buttons - through the same calls the dev build's Wi-Fi
// link feeds (devlink.h). One step a line:
//   launch FILE        opens that game from the list
//   wait N             N loops of the app (a loop is a screen refresh)
//   press KEYS [N]     a+b, start, up ... zl, zr held for N loops (default 4), then let go
//   tap X Y [N]        the stylus on the bottom screen for N loops (default 4)
//   shot NAME          both screens to sdmc:/pokedaisy/shots/NAME.bmp (400 x 480: the top over the bottom)
//   log TEXT           a line in log.txt
//   leave              leaves the running game
//   quit               closes the app ("autotest done" in log.txt)
// GPL-3.0 (see NOTICE).
#ifndef PD_AUTOTEST_H
#define PD_AUTOTEST_H

#include <stdbool.h>
#include <stdint.h>

void autotest_start(const char* scriptPath, const char* shotDir);
uint32_t autotest_keys(void);
bool autotest_touch(int* x, int* y);
bool autotest_take_launch(char* out, unsigned outLen);
bool autotest_take_leave(void);
bool autotest_quit_wanted(void);
// Once a loop, after the frame is on screen: the script's clock.
void autotest_after_frame(void);

#endif
