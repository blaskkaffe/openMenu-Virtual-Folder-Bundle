/* bios_surface: see bios_surface.h. */
#include "bios_surface.h"

#include <stdlib.h>
#include <string.h>

#include "bios_text.h"

static bsurf surfs[BSURF_MAX];
static const uint8_t* font;
static const uint8_t* digits;
static const uint8_t* clock_glyphs;

void
bsurf_init(const bios_rom* rom) {
    font = rom ? bios_rom_ptr(rom, 0x8C000000u + BTEXT_FONT_OFFSET, 0x80000u) : NULL;
    digits = rom ? bios_rom_ptr(rom, BTEXT_TINY_DIGITS, 240) : NULL;
    clock_glyphs = rom ? bios_rom_ptr(rom, 0x8C071AB0u, 14 * 256) : NULL;
}

const uint8_t*
bsurf_font(void) {
    return font;
}

bsurf*
bsurf_find(uint16_t id) {
    for (int i = 0; i < BSURF_MAX; i++) {
        if (surfs[i].id == id && surfs[i].px) {
            return &surfs[i];
        }
    }
    return NULL;
}

bsurf*
bsurf_get(uint16_t id, int w, int h) {
    bsurf* s = bsurf_find(id);
    if (s && s->w == w && s->h == h) {
        return s;
    }
    if (s) {
        bsurf_free(id);
    }
    for (int i = 0; i < BSURF_MAX; i++) {
        if (!surfs[i].px) {
            s = &surfs[i];
            memset(s, 0, sizeof(*s));
            s->px = (uint16_t*)malloc((size_t)w * (size_t)h * 2u);
            if (!s->px) {
                return NULL;
            }
            s->id = id;
            s->w = w;
            s->h = h;
            s->colour = 0xFCCC;
            s->advance = BTEXT_ADVANCE;
            bsurf_clear(s);
            return s;
        }
    }
    return NULL;
}

void
bsurf_free(uint16_t id) {
    bsurf* s = bsurf_find(id);
    if (s) {
        free(s->px);
        memset(s, 0, sizeof(*s));
    }
}

void
bsurf_free_all(void) {
    for (int i = 0; i < BSURF_MAX; i++) {
        free(surfs[i].px);
        memset(&surfs[i], 0, sizeof(surfs[i]));
    }
}

void
bsurf_touch(bsurf* s, int y0, int y1) {
    if (!s) {
        return;
    }
    y0 = y0 < 0 ? 0 : y0;
    y1 = y1 >= s->h ? s->h - 1 : y1;
    if (y0 > y1) {
        return;
    }
    if (s->dirty_y0 > s->dirty_y1) {
        s->dirty_y0 = y0;
        s->dirty_y1 = y1;
    } else {
        s->dirty_y0 = y0 < s->dirty_y0 ? y0 : s->dirty_y0;
        s->dirty_y1 = y1 > s->dirty_y1 ? y1 : s->dirty_y1;
    }
    s->serial++;
}

int
bsurf_take_dirty(bsurf* s, int* y0, int* y1) {
    if (!s || s->dirty_y0 > s->dirty_y1) {
        return 0;
    }
    *y0 = s->dirty_y0;
    *y1 = s->dirty_y1;
    s->dirty_y0 = 1;
    s->dirty_y1 = 0;
    return 1;
}

void
bsurf_clear(bsurf* s) {
    if (!s) {
        return;
    }
    for (int i = 0; i < s->w * s->h; i++) {
        s->px[i] = s->bg;
    }
    bsurf_touch(s, 0, s->h - 1);
}

void
bsurf_fill(bsurf* s, int x, int y, int w, int h, uint16_t colour) {
    if (!s) {
        return;
    }
    int x1 = x + w > s->w ? s->w : x + w, y1 = y + h > s->h ? s->h : y + h;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    for (int r = y; r < y1; r++) {
        for (int c = x; c < x1; c++) {
            s->px[r * s->w + c] = colour;
        }
    }
    bsurf_touch(s, y, y1 - 1);
}

void
bsurf_print(bsurf* s, int x, int y, const char* str) {
    if (!s || !str || !font) {
        return;
    }
    btext_rich(s->px, s->w, s->h, x, y, font, str, &s->colour, s->advance);
    bsurf_touch(s, y, y + BTEXT_SYMBOL_W + 1);
}

void
bsurf_format_number(char* out, int value, int width) {
    /* int_to_digits: up to 8 digits; leading zeros become spaces when width < 0; the last |width| characters */
    char buf[12];
    int pad_spaces = width < 0;
    int w = width < 0 ? -width : width;
    unsigned v = value < 0 ? (unsigned)-value : (unsigned)value;
    w = w > 8 ? 8 : w;
    for (int i = 7; i >= 0; i--) {
        buf[i] = (char)('0' + v % 10);
        v /= 10;
    }
    buf[8] = '\0';
    if (pad_spaces) {
        for (int i = 0; i < 7 && buf[i] == '0'; i++) {
            buf[i] = ' ';
        }
    }
    memcpy(out, buf + 8 - w, (size_t)w + 1);
}

void
bsurf_print_number(bsurf* s, int x, int y, int value, int width) {
    char text[12];
    bsurf_format_number(text, value, width);
    bsurf_print(s, x, y, text);
}

void
bsurf_tiny_number(bsurf* s, int x, int y, int value, int width) {
    if (!s || !digits) {
        return;
    }
    char text[12];
    bsurf_format_number(text, value, width);
    for (const char* p = text; *p; p++) {
        if (*p != ' ') {
            btext_tiny_digit(s->px, s->w, s->h, x, y, digits, *p - '0', s->colour);
        }
        x += s->advance;
    }
    bsurf_touch(s, y, y + 11);
}

void
bsurf_clock_text(bsurf* s, int x, int y, const char* str) {
    if (!s || !str || !clock_glyphs) {
        return;
    }
    for (const char* p = str; *p; p++, x += 11) {
        int g;
        switch (*p) {
            case '.': g = 13; break;
            case '/': g = 11; break;
            case ':': g = 12; break;
            default: g = (*p >= '0' && *p <= '9') ? *p - '0' + 1 : 0; break;
        }
        const uint8_t* src = clock_glyphs + g * 256;
        for (int r = 0; r < 16; r++) {
            int py = y + r;
            if (py < 0 || py >= s->h) {
                continue;
            }
            for (int c = 0; c < 16; c++) {
                int px = x + c;
                if (px >= 0 && px < s->w) {
                    /* (b & 0xF0) * -0x100 - 0x1000: alpha 15 - b / 16, black */
                    s->px[py * s->w + px] = (uint16_t)((15 - (src[r * 16 + c] >> 4)) << 12);
                }
            }
        }
    }
    bsurf_touch(s, y, y + 15);
}

void
bsurf_jis(bsurf* s, int x, int y, unsigned jis) {
    if (!s || !font) {
        return;
    }
    btext_blit24(s->px, s->w, s->h, x, y, btext_jis_glyph(font, jis), s->colour);
    bsurf_touch(s, y, y + 25);
}
