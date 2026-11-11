// Between pd_emu.c and pd_emu_call.c, the two files that see armGBA's own headers. GPL-3.0 (see NOTICE).
#ifndef PD_EMU_INTERNAL_H
#define PD_EMU_INTERNAL_H

struct GBA; // armGBA's

// One instruction on armGBA's exact CPU (its interpreter), or the wait for the next event while the CPU is halted:
// what its run loop does between compiled blocks.
void pd_emu_step(struct GBA* gba);

#endif
