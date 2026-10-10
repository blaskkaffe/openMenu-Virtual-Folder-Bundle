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

/* ---- The BIOS message strings (text_render_string 0x8C01526C) ----
 * Bytes 0x10..0x1F set the colour (btext_code_colour), 0x01 n draws symbol n of the 24x24 set (the controller
 * buttons; 25 pixels wide), a space moves 8 pixels and any other byte is a glyph `advance` pixels wide. */
#define BTEXT_SYMBOL_W 24
#define BTEXT_SYMBOL_ADVANCE 25
#define BTEXT_FONT_OFFSET 0x100020u    /* the font block in the boot ROM (what syscall_font_address() returns) */
#define BTEXT_SYMBOLS 0x7E900u         /* 22 symbols of 24x24 bits after the font base */
#define BTEXT_VMU_ICONS 0x7EF30u       /* 129 VMU pictures of 32x32 bits after the font base */

/* Colour of control byte 0x10..0x1F (ARGB4444); 0 for other bytes. */
uint16_t btext_code_colour(int code);

/* Width of a message string as the BIOS measures it for centring: `advance` for every byte but a space (8). */
int btext_rich_width(const char* s, int advance);

/* Draw a message string at (x, y). x < 0 centres it on -x, as the BIOS does with x = -256 in a 512 wide surface.
 * `colour` is the current colour; control bytes change it (and it stays changed, as in the BIOS). */
void btext_rich(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* font, const char* s, uint16_t* colour,
                int advance);

/* One 24x24 symbol (0x8C027CC0). */
void btext_symbol(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* font, int n, uint16_t colour);

/* Message `id` line `line` (0..3) of the boot ROM's table for `language` (0 Japanese, 1 English, 2 German, 3 French,
 * 4 Spanish, 5 Italian): msg_get_line 0x8C0170E0. NULL when there is no such line. Points into the ROM. */
struct bios_rom;
const char* btext_message(const struct bios_rom* rom, int language, int id, int line);

/* The small 12x12 digits of the file tiles (0x8C0719C0; pixels that are clear in the table are drawn). */
void btext_tiny_digit(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* table, int digit, uint16_t colour);
#define BTEXT_TINY_DIGITS 0x8C0719C0u /* RAM address of the table in the menu image */

#ifdef __cplusplus
}
#endif

#endif /* BIOS_TEXT_H */
