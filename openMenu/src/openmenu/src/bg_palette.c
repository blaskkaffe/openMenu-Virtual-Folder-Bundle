#include "bg_palette.h"

/* Same pale-top, saturated-bottom shape as the BIOS gradient (0xB0D0D0 / 0x4060C0), in a few other hues. */
static const struct {
    const char* name;
    uint32_t top, bottom;
} palette[BG_PALETTE_COUNT] = {
    {"BIOS blue", 0, 0},
    {"Green", 0xFFB0D0B0u, 0xFF30A050u},
    {"Orange", 0xFFE0C8A0u, 0xFFC07030u},
    {"Purple", 0xFFC8B0D8u, 0xFF7040B0u},
    {"Red", 0xFFD8B0B0u, 0xFFB03038u},
    {"Grey", 0xFFC8C8C8u, 0xFF606068u},
};

const char*
bg_palette_name(int index) {
    return index >= 0 && index < BG_PALETTE_COUNT ? palette[index].name : "?";
}

static uint32_t
dim(uint32_t argb) {
    uint32_t r = ((argb >> 16) & 0xFF) * 3 / 8, g = ((argb >> 8) & 0xFF) * 3 / 8, b = (argb & 0xFF) * 3 / 8;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

void
bg_palette_apply(int index, int dark, uint32_t* top, uint32_t* bottom) {
    if (index > 0 && index < BG_PALETTE_COUNT) {
        *top = palette[index].top;
        *bottom = palette[index].bottom;
    }
    if (dark) {
        *top = dim(*top);
        *bottom = dim(*bottom);
    }
}

uint32_t
bg_palette_wave_tint(int index, int dark) {
    uint32_t r = 255, g = 255, b = 255;
    if (index > 0 && index < BG_PALETTE_COUNT) {
        /* the BIOS gradient ends in 0x4060C0: the ratio of each channel of the new colour to it, the largest channel brought to full */
        uint32_t bot = palette[index].bottom;
        float fr = (float)((bot >> 16) & 0xFF) / 64.0f, fg = (float)((bot >> 8) & 0xFF) / 96.0f, fb = (float)(bot & 0xFF) / 192.0f;
        float mx = fr > fg ? (fr > fb ? fr : fb) : (fg > fb ? fg : fb);
        r = (uint32_t)(255.0f * fr / mx);
        g = (uint32_t)(255.0f * fg / mx);
        b = (uint32_t)(255.0f * fb / mx);
    }
    if (dark) {
        r = r * 3 / 8;
        g = g * 3 / 8;
        b = b * 3 / 8;
    }
    return r << 16 | g << 8 | b;
}

uint32_t
bg_palette_bar_dark(void) {
    return 0xFF3C3C40u;
}
