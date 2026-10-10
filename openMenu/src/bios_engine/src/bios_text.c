#include "bios_text.h"

#include "bios_rom.h"

const uint8_t*
btext_glyph(const uint8_t* font, uint32_t ch) {
    uint32_t index = 288; /* an empty glyph, as the system font routine does for unknown characters */
    if (ch >= 33 && ch <= 126) {
        index = ch - 32;
    } else if (ch >= 160 && ch <= 255) {
        index = ch - (160 - 96);
    }
    return font + index * BTEXT_GLYPH_BYTES;
}

uint16_t
btext_shade(uint16_t colour) {
    return (uint16_t)(((colour & 0xCCCu) >> 2) | 0xF000u);
}

static void
put(uint16_t* canvas, int stride, int height, int x, int y, uint16_t v) {
    if (x >= 0 && x < stride && y >= 0 && y < height) {
        canvas[y * stride + x] = v;
    }
}

void
btext_blit(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* glyph, uint16_t colour) {
    uint16_t shade = btext_shade(colour);
    for (int row = 0; row < BTEXT_GLYPH_H; row += 2) {
        const uint8_t* g = glyph + (row / 2) * 3;
        uint32_t bits = ((uint32_t)g[0] << 16) | ((uint32_t)g[1] << 8) | g[2];
        for (int half = 0; half < 2; half++) {
            for (int col = 0; col < BTEXT_GLYPH_W; col++) {
                if (bits & (0x800000u >> (half * BTEXT_GLYPH_W + col))) {
                    int px = x + col, py = y + row + half;
                    put(canvas, stride, height, px, py, colour);
                    put(canvas, stride, height, px + 1, py, colour);
                    put(canvas, stride, height, px, py + 1, colour);
                    put(canvas, stride, height, px + 1, py + 1, shade);
                    put(canvas, stride, height, px + 2, py + 1, shade);
                }
            }
        }
    }
}

int
btext_width(const char* s) {
    int w = 0;
    for (; *s; s++) {
        w += *s == ' ' ? BTEXT_SPACE : BTEXT_ADVANCE;
    }
    return w;
}

int
btext_draw(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* font, const char* s, uint16_t colour) {
    int start = x;
    for (; *s; s++) {
        if (*s == ' ') {
            x += BTEXT_SPACE;
            continue;
        }
        btext_blit(canvas, stride, height, x, y, btext_glyph(font, (unsigned char)*s), colour);
        x += BTEXT_ADVANCE;
    }
    return x - start;
}

/* ---- message strings ------------------------------------------------------------------------------------ */

uint16_t
btext_code_colour(int code) {
    /* text_render_string: the colour each control byte selects */
    static const uint16_t table[16] = {0xF000, 0xF00C, 0xFC00, 0xFC0C, 0xF0C0, 0xF0CC, 0xFCC0, 0xFCCC,
                                       0x7CCC, 0xF006, 0xF600, 0xF606, 0xF060, 0xF066, 0xF660, 0xF666};
    return code >= 0x10 && code <= 0x1F ? table[code - 0x10] : 0;
}

int
btext_rich_width(const char* s, int advance) {
    /* As text_render_string measures a string to centre it (Latin languages): every byte but a space counts as a
     * glyph, the control bytes too, so a line with a button symbol sits a little to the left. */
    int w = 0;
    for (const unsigned char* p = (const unsigned char*)s; p && *p; p++) {
        w += *p == ' ' ? BTEXT_SPACE : advance;
    }
    return w;
}

void
btext_symbol(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* font, int n, uint16_t colour) {
    if (n < 0 || n >= 0x16) {
        n = 0;
    }
    btext_blit24(canvas, stride, height, x, y, font + BTEXT_SYMBOLS + (uint32_t)n * 72u, colour);
}

const uint8_t*
btext_jis_glyph(const uint8_t* font, unsigned jis) {
    unsigned row = jis >> 8, col = jis & 0xFF;
    if (row < 0x21 || row > 0x28 || col < 0x21 || col > 0x7E) {
        return NULL;
    }
    return font + 288u * BTEXT_GLYPH_BYTES + ((row - 0x21) * 94u + (col - 0x21)) * 72u;
}

void
btext_blit24(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* g, uint16_t colour) {
    if (!g) {
        return;
    }
    uint16_t shade = btext_shade(colour);
    for (int row = 0; row < BTEXT_SYMBOL_W; row++) {
        uint32_t bits = ((uint32_t)g[row * 3] << 16) | ((uint32_t)g[row * 3 + 1] << 8) | g[row * 3 + 2];
        for (int col = 0; col < BTEXT_SYMBOL_W; col++) {
            if (bits & (0x800000u >> col)) {
                int px = x + col, py = y + row;
                put(canvas, stride, height, px, py, colour);
                put(canvas, stride, height, px + 1, py, colour);
                put(canvas, stride, height, px, py + 1, colour);
                put(canvas, stride, height, px + 1, py + 1, shade);
                put(canvas, stride, height, px + 2, py + 1, shade);
            }
        }
    }
}

void
btext_rich(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* font, const char* s, uint16_t* colour,
           int advance) {
    if (!s) {
        return;
    }
    if (x < 0) {
        x = -x - btext_rich_width(s, advance) / 2;
    }
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p == 1) {
            if (!p[1]) {
                break;
            }
            btext_symbol(canvas, stride, height, x, y, font, p[1], *colour);
            x += BTEXT_SYMBOL_ADVANCE;
            p += 2;
        } else if (*p >= 0x10 && *p <= 0x1F) {
            *colour = btext_code_colour(*p);
            p++;
        } else if (*p == ' ') {
            x += BTEXT_SPACE;
            p++;
        } else {
            btext_blit(canvas, stride, height, x, y, btext_glyph(font, *p), *colour);
            x += advance;
            p++;
        }
    }
}

void
btext_tiny_digit(uint16_t* canvas, int stride, int height, int x, int y, const uint8_t* table, int digit, uint16_t colour) {
    if (digit < 0 || digit > 9) {
        return;
    }
    const uint8_t* g = table + digit * 24;
    for (int row = 0; row < 12; row++) {
        unsigned bits = ((unsigned)g[row * 2] << 8) | g[row * 2 + 1];
        for (int col = 0; col < 12; col++) {
            if (!(bits & (0x8000u >> col))) {
                put(canvas, stride, height, x + col, y + row, colour);
            }
        }
    }
}

const char*
btext_message(const struct bios_rom* rom, int language, int id, int line) {
    /* msg_table_jp, _en, _de, _fr, _es, _it: entries of 20 bytes (u16 id, u16 lines, 4 string pointers), 0xFFFF ends */
    static const uint32_t tables[6] = {0x8C039460u, 0x8C039BA4u, 0x8C03AA2Cu, 0x8C03A2E8u, 0x8C03B170u, 0x8C03B8B4u};
    if (!rom || line < 0 || line > 3) {
        return NULL;
    }
    uint32_t t = tables[language >= 0 && language < 6 ? language : 1];
    for (int i = 0; i < 2000; i++) {
        const uint8_t* e = bios_rom_ptr(rom, t + (uint32_t)i * 20u, 20);
        if (!e) {
            return NULL;
        }
        unsigned eid = (unsigned)e[0] | ((unsigned)e[1] << 8);
        if (eid == 0xFFFFu) {
            return NULL;
        }
        if (eid == (unsigned)id) {
            const uint8_t* q = e + 4 + line * 4;
            uint32_t addr = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
            return addr ? (const char*)bios_rom_ptr(rom, addr, 1) : NULL;
        }
    }
    return NULL;
}
