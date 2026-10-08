/*
 * tex_decode: PVR texture payloads to ARGB8888, see tex_decode.h.
 */
#include "tex_decode.h"

uint32_t
bios_untwiddle(uint32_t x, uint32_t y) {
    uint32_t r = 0;
    for (uint32_t b = 0; (1u << b) <= (x > y ? x : y); b++) {
        r |= (((y >> b) & 1u) << (2 * b)) | (((x >> b) & 1u) << (2 * b + 1));
    }
    return r;
}

static uint32_t
convert(uint16_t v, int fmt) {
    uint32_t a, r, g, b;
    switch (fmt) {
        case BIOS_PVR_ARGB1555:
            a = (v & 0x8000) ? 255 : 0;
            r = ((v >> 10) & 31) * 255 / 31;
            g = ((v >> 5) & 31) * 255 / 31;
            b = (v & 31) * 255 / 31;
            break;
        case BIOS_PVR_RGB565:
            a = 255;
            r = ((v >> 11) & 31) * 255 / 31;
            g = ((v >> 5) & 63) * 255 / 63;
            b = (v & 31) * 255 / 31;
            break;
        default: /* ARGB4444 */
            a = ((v >> 12) & 15) * 17;
            r = ((v >> 8) & 15) * 17;
            g = ((v >> 4) & 15) * 17;
            b = (v & 15) * 17;
            break;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint16_t
px16(const uint8_t* p, size_t i) {
    return (uint16_t)(p[i * 2] | (p[i * 2 + 1] << 8));
}

int
bios_texture_decode(const bios_texture* t, uint32_t* argb) {
    if (!t || !argb || !t->data || t->data_size == 0) {
        return -1;
    }
    uint32_t w = t->width, h = t->height;
    switch (t->data_type) {
        case BIOS_PVR_TWIDDLED:
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    argb[y * w + x] = convert(px16(t->data, bios_untwiddle(x, y)), t->pixel_format);
                }
            }
            return 0;
        case BIOS_PVR_RECTANGLE:
            for (uint32_t i = 0; i < w * h; i++) {
                argb[i] = convert(px16(t->data, i), t->pixel_format);
            }
            return 0;
        case BIOS_PVR_VQ: {
            const uint8_t* index = t->data + 2048;
            for (uint32_t y = 0; y < h / 2; y++) {
                for (uint32_t x = 0; x < w / 2; x++) {
                    uint32_t e = index[bios_untwiddle(x, y)];
                    for (uint32_t k = 0; k < 4; k++) {
                        argb[(2 * y + (k & 1)) * w + 2 * x + (k >> 1)] = convert(px16(t->data, e * 4 + k), t->pixel_format);
                    }
                }
            }
            return 0;
        }
        default: return -1;
    }
}
