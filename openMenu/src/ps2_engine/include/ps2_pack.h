/*
 * ps2_pack: reader of PS2.PAK, the asset pack of the PS2-style menu (textures, icon models,
 * font metrics), made by tools/ps2-assets/ps2pack.py from a PS2 BIOS export. The file layout is
 * described there. The buffer given to ps2_pack_open() must stay alive and 4-byte aligned while
 * the pack is used; models and pixels are read in place. Portable C.
 */
#ifndef PS2_PACK_H
#define PS2_PACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_PACK_MAX_TEX 64
#define PS2_PACK_MAX_MODELS 16
#define PS2_FONT_MAX_GLYPHS 96

enum { PS2_FMT_ARGB4444 = 0, PS2_FMT_RGB565 = 1, PS2_FMT_ARGB1555 = 2 };

typedef struct ps2_tex_info {
    char name[17];
    int w, h;            /* texture size (powers of two) */
    int fmt;             /* PS2_FMT_* */
    int pic_w, pic_h;    /* the picture inside the (padded) texture */
    const uint8_t* pixels; /* w * h * 2 bytes, row by row */
    uint32_t size;
} ps2_tex_info;

typedef struct ps2_model_info {
    char name[17];
    int nverts;            /* a triangle list */
    const float* v;        /* 8 floats per vertex: x y z nx ny nz u v */
} ps2_model_info;

typedef struct ps2_glyph {
    uint8_t left, width, advance; /* drawn part inside the cell, and the pen step */
} ps2_glyph;

typedef struct ps2_pack {
    int ntex;
    ps2_tex_info tex[PS2_PACK_MAX_TEX];
    int nmodels;
    ps2_model_info model[PS2_PACK_MAX_MODELS];
    int font_tex;           /* texture index of the font sheet, -1 if there is none */
    int glyph_first, glyph_count, cell_w, cell_h;
    ps2_glyph glyph[PS2_FONT_MAX_GLYPHS];
} ps2_pack;

enum { PS2_PACK_OK = 0, PS2_PACK_ERR_MAGIC = -1, PS2_PACK_ERR_RANGE = -2, PS2_PACK_ERR_EMPTY = -3 };

/* Parse the directory. Returns PS2_PACK_OK or a negative PS2_PACK_ERR_*. */
int ps2_pack_open(ps2_pack* p, const void* data, size_t size);

/* Index of a texture / model by name, -1 if the pack has none. */
int ps2_pack_find_tex(const ps2_pack* p, const char* name);
int ps2_pack_find_model(const ps2_pack* p, const char* name);

#ifdef __cplusplus
}
#endif

#endif /* PS2_PACK_H */
