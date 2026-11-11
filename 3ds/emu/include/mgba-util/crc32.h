// See ../mgba/core/core.h: the seam's stand-in for mGBA's header of this name. zlib's CRC-32, as mGBA's doCrc32.
#ifndef PD_EMU_CRC32_H
#define PD_EMU_CRC32_H
#include <stddef.h>
#include <stdint.h>
uint32_t doCrc32(const void* buf, size_t size);
#endif
