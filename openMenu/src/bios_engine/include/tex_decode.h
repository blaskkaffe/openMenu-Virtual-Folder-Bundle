/*
 * tex_decode: PVR texture payloads to ARGB8888, for host tools and tests.
 * The console never needs this: the PVR reads the payload as is.
 */
#ifndef TEX_DECODE_H
#define TEX_DECODE_H

#include <stdint.h>

#include "bios_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Morton interleave used by twiddled textures (x occupies the odd bits). */
uint32_t bios_untwiddle(uint32_t x, uint32_t y);

/* Decode into `argb` (width * height entries). Returns 0, -1 for unsupported data. */
int bios_texture_decode(const bios_texture* tex, uint32_t* argb);

#ifdef __cplusplus
}
#endif

#endif /* TEX_DECODE_H */
