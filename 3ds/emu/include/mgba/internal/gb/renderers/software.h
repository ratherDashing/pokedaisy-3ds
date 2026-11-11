// See ../../../core/core.h: the seam's stand-in for mGBA's header of this name. Gambatte keeps its colours to itself, so
// the palette here is a plain grey ramp: pk_wide_gb.c still tells the Game Boy's scene by it, but the 3DS build doesn't
// draw EXPAND from it.
#ifndef PD_EMU_MGBA_GB_SOFTWARE_H
#define PD_EMU_MGBA_GB_SOFTWARE_H

#include <mgba/internal/gb/gb.h>

struct GBVideoSoftwareRenderer {
    struct GBVideoRenderer d;
    uint16_t palette[192];
    uint8_t lookup[192];
    enum GBModel model;
};

#endif
