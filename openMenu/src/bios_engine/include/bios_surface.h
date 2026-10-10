/*
 * bios_surface: the BIOS menu's text surfaces (text_surface_alloc and friends, 0x8C014B6C..0x8C015A80): ARGB4444
 * pictures that an object shows as a sprite centred on its position plus its text offset. The screens print
 * messages into them, draw the file tiles, VMU icons and so on, exactly as the BIOS writes into its own surfaces.
 * A surface belongs to an object id; the renderer looks it up when the object's text is drawn (bscene sink->text).
 * Portable C.
 */
#ifndef BIOS_SURFACE_H
#define BIOS_SURFACE_H

#include <stdint.h>

#include "bios_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSURF_MAX 24

typedef struct bsurf {
    uint16_t id;      /* object id, 0 = free */
    int w, h;         /* pixels; w is a power of two */
    uint16_t* px;     /* w * h ARGB4444 */
    uint16_t bg;      /* clear colour (text_bgcol) */
    uint16_t colour;  /* text colour (text_set_colour) */
    int advance;      /* pixels per glyph (text_set_advance) */
    int hidden;       /* not drawn (text_set_field2) */
    int dirty_y0, dirty_y1; /* rows changed since the renderer last took the picture; y0 > y1 = none */
    uint32_t serial;  /* bumped on every change */
} bsurf;

/* The font and the tile digits are read from the boot ROM. */
void bsurf_init(const bios_rom* rom);
const uint8_t* bsurf_font(void);

/* The surface of object `id`, created (cleared to transparent) if there is none or its size differs. NULL if out
 * of memory or slots. */
bsurf* bsurf_get(uint16_t id, int w, int h);
bsurf* bsurf_find(uint16_t id);
void bsurf_free(uint16_t id);
void bsurf_free_all(void);

void bsurf_clear(bsurf* s);
void bsurf_fill(bsurf* s, int x, int y, int w, int h, uint16_t colour);
/* text_print: a message string at (x, y), x < 0 centres it on -x. */
void bsurf_print(bsurf* s, int x, int y, const char* str);
/* text_print_number: `value` in `width` characters (negative width: right aligned, padded with spaces). */
void bsurf_print_number(bsurf* s, int x, int y, int value, int width);
/* FUN_8C01583E: the same with the small digits of the file tiles. */
void bsurf_tiny_number(bsurf* s, int x, int y, int value, int width);
/* FUN_8C021E60: the date and time of the header bar in its own small font: 16x16 glyphs with 16 levels of alpha
 * (0x8C071AB0), black, 11 pixels apart. Draws digits, ' ', '.', '/' and ':'. */
void bsurf_clock_text(bsurf* s, int x, int y, const char* str);

/* Mark rows y0..y1 changed (after writing into px directly). */
void bsurf_touch(bsurf* s, int y0, int y1);

/* The renderer: take the changed rows (returns 0 if nothing changed) and reset them. */
int bsurf_take_dirty(bsurf* s, int* y0, int* y1);

/* int_to_digits 0x8C015910: `value` as text in |width| characters; width < 0 pads with spaces on the left
 * (right aligned), width > 0 with zeros. */
void bsurf_format_number(char* out, int value, int width);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_SURFACE_H */
