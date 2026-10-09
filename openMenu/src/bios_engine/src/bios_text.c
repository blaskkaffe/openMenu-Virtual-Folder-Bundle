#include "bios_text.h"

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
