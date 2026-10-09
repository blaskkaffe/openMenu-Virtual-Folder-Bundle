/*
 * bios_text: the BIOS menu's way of drawing text, ported from the boot ROM decompile (glyph blits at
 * 0x8C027D64 and 0x8C027CC0). Each set pixel of the 12x24 ROM glyph writes the glyph colour at x, x+1 and
 * below it, and a quarter-brightness shade at the two pixels down-right of that: the "double thickness"
 * look of the BIOS text. Portable, 16 bit ARGB4444 canvas.
 */
#ifndef BIOS_TEXT_H
#define BIOS_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTEXT_GLYPH_W 12
#define BTEXT_GLYPH_H 24
#define BTEXT_GLYPH_BYTES 36 /* 24 rows of 12 bits, two rows per 3 bytes, most significant bit first */
#define BTEXT_ADVANCE 11     /* pixels from one narrow glyph to the next (the BIOS default) */
#define BTEXT_SPACE 8        /* a space moves 8 pixels */

/* Glyph of Latin-1 character `ch` in the ROM font block (what syscall_font_address() returns, ROM
 * offset 0x100020). Characters the font does not have give an empty glyph. */
const uint8_t* btext_glyph(const uint8_t* font, uint32_t ch);

/* The colour of the shade pixels for a glyph colour (ARGB4444): a quarter of the brightness, opaque. */
uint16_t btext_shade(uint16_t colour);

/* Draw one glyph with its top-left at (x, y); pixels outside the canvas are clipped. */
void btext_blit(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* glyph, uint16_t colour);

/* Width of a string in pixels (without the shade pixels that stick out 2 px at the right). */
int btext_width(const char* s);

/* Draw a string at (x, y); returns its width. */
int btext_draw(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* font, const char* s, uint16_t colour);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_TEXT_H */
