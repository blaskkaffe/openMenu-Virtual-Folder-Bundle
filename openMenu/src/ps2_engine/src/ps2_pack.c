/* ps2_pack: reader of PS2.PAK, see ps2_pack.h. */
#include <string.h>

#include "ps2_pack.h"

enum { KIND_TEX = 1, KIND_MODEL = 2, KIND_FONT = 3 };
#define HEADER_SIZE 16
#define ENTRY_SIZE 48

static uint32_t
rd32(const uint8_t* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void
copy_name(char* out, const uint8_t* in) {
    memcpy(out, in, 16);
    out[16] = '\0';
}

int
ps2_pack_open(ps2_pack* p, const void* data, size_t size) {
    const uint8_t* b = (const uint8_t*)data;
    memset(p, 0, sizeof(*p));
    p->font_tex = -1;
    if (!b || size < HEADER_SIZE || memcmp(b, "PS2P", 4) != 0 || rd32(b + 4) != 1) {
        return PS2_PACK_ERR_MAGIC;
    }
    uint32_t count = rd32(b + 8);
    uint32_t dir = rd32(b + 12);
    if (dir > size || count > (size - dir) / ENTRY_SIZE) {
        return PS2_PACK_ERR_RANGE;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* e = b + dir + (size_t)i * ENTRY_SIZE;
        uint32_t kind = rd32(e + 16), off = rd32(e + 20), len = rd32(e + 24);
        uint32_t q[5];
        for (int k = 0; k < 5; k++) {
            q[k] = rd32(e + 28 + 4 * k);
        }
        if (off > size || len > size - off) {
            return PS2_PACK_ERR_RANGE;
        }
        if (kind == KIND_TEX && p->ntex < PS2_PACK_MAX_TEX) {
            ps2_tex_info* t = &p->tex[p->ntex];
            copy_name(t->name, e);
            t->w = (int)q[0];
            t->h = (int)q[1];
            t->fmt = (int)q[2];
            t->pic_w = (int)q[3];
            t->pic_h = (int)q[4];
            t->pixels = b + off;
            t->size = len;
            if (t->w <= 0 || t->h <= 0 || (uint64_t)t->w * (uint64_t)t->h * 2u > len) {
                return PS2_PACK_ERR_RANGE;
            }
            if (!strcmp(t->name, "FONT")) {
                p->font_tex = p->ntex;
            }
            p->ntex++;
        } else if (kind == KIND_MODEL && p->nmodels < PS2_PACK_MAX_MODELS) {
            ps2_model_info* m = &p->model[p->nmodels];
            copy_name(m->name, e);
            m->nverts = (int)q[0];
            if (m->nverts <= 0 || (off & 3u) || (uint64_t)m->nverts * 32u > len) {
                return PS2_PACK_ERR_RANGE;
            }
            m->v = (const float*)(const void*)(b + off);
            p->nmodels++;
        } else if (kind == KIND_FONT) {
            p->glyph_first = (int)q[0];
            p->glyph_count = (int)q[1] > PS2_FONT_MAX_GLYPHS ? PS2_FONT_MAX_GLYPHS : (int)q[1];
            p->cell_w = (int)q[2];
            p->cell_h = (int)q[3];
            if ((uint64_t)p->glyph_count * 4u > len) {
                return PS2_PACK_ERR_RANGE;
            }
            for (int g = 0; g < p->glyph_count; g++) {
                p->glyph[g].left = b[off + 4 * (uint32_t)g];
                p->glyph[g].width = b[off + 4 * (uint32_t)g + 1];
                p->glyph[g].advance = b[off + 4 * (uint32_t)g + 2];
            }
        }
    }
    if (p->ntex == 0 || p->font_tex < 0 || p->glyph_count == 0) {
        return PS2_PACK_ERR_EMPTY;
    }
    return PS2_PACK_OK;
}

int
ps2_pack_find_tex(const ps2_pack* p, const char* name) {
    for (int i = 0; i < p->ntex; i++) {
        if (!strcmp(p->tex[i].name, name)) {
            return i;
        }
    }
    return -1;
}

int
ps2_pack_find_model(const ps2_pack* p, const char* name) {
    for (int i = 0; i < p->nmodels; i++) {
        if (!strcmp(p->model[i].name, name)) {
            return i;
        }
    }
    return -1;
}
