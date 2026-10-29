/*
 * ps2_draw: drawing helpers of the PS2-style menu: textured and flat quads, text in the PS2
 * font, and the icon models (rotated, lit, projected, painter-sorted). They only produce
 * screen-space triangles for a sink (the PVR on the console, a software rasteriser in the PC
 * preview), so everything here is portable C. Triangles reach the sink back to front: the sink
 * must blend in submission order and not depth test.
 */
#ifndef PS2_DRAW_H
#define PS2_DRAW_H

#include <stdint.h>

#include "ps2_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_SCREEN_W 640.0f
#define PS2_SCREEN_H 480.0f

#define PS2_TEX_NONE (-1)
#define PS2_TEX_ART 0x1000 /* + slot: the game picture bound to that slot (see bind_art) */
#define PS2_ART_SLOTS 12

typedef enum { PS2_BLEND_ALPHA = 0, PS2_BLEND_ADD = 1 } ps2_blend;

typedef struct ps2_vtx {
    float x, y, z; /* screen pixels, z = 1/w for the PVR (larger is nearer) */
    float u, v;
    uint32_t argb;
} ps2_vtx;

typedef struct ps2_sink {
    void* user;
    /* One triangle. tex is a pack texture index, PS2_TEX_ART + slot, or PS2_TEX_NONE. */
    void (*tri)(void* user, const ps2_vtx v[3], int tex, int blend);
    /* The game whose picture PS2_TEX_ART + slot shows from now on. */
    void (*bind_art)(void* user, int slot, const char* product);
} ps2_sink;

typedef struct ps2_draw {
    const ps2_pack* pack;
    ps2_sink sink;
    float alpha;   /* multiplies the alpha of everything drawn (fades) */
    float ox, oy;  /* added to every screen position (slides) */
    unsigned tris; /* triangles sent since the counter was cleared */
} ps2_draw;

void ps2d_init(ps2_draw* d, const ps2_pack* pack, const ps2_sink* sink);

static inline uint32_t
ps2_argb(int a, int r, int g, int b) {
    return ((uint32_t)(a & 255) << 24) | ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}

/* Colour with its alpha scaled by f (0..1) */
uint32_t ps2_fade(uint32_t argb, float f);
/* Linear mix of two colours, t = 0..1 */
uint32_t ps2_mix(uint32_t a, uint32_t b, float t);

/* Quad with texture coordinates (u0,v0)-(u1,v1) given in 0..1 of the picture inside the texture;
 * tex may be PS2_TEX_NONE for a flat colour. */
void ps2d_quad(ps2_draw* d, int tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
               uint32_t argb, int blend);
/* Whole picture of a texture centred on (cx, cy), w x h pixels, turned by `angle` radians. */
void ps2d_sprite(ps2_draw* d, int tex, float cx, float cy, float w, float h, float angle, uint32_t argb, int blend);
/* Flat quad with a vertical colour gradient */
void ps2d_gradient(ps2_draw* d, float x, float y, float w, float h, uint32_t top, uint32_t bottom, int blend);
/* Flat quad with a horizontal colour gradient */
void ps2d_gradient_h(ps2_draw* d, float x, float y, float w, float h, uint32_t left, uint32_t right, int blend);
void ps2d_rect(ps2_draw* d, float x, float y, float w, float h, uint32_t argb);

/* ---- Text: the PS2 font, scale 1.0 = 40 pixels per line --------------------------------- */

enum { PS2_TEXT_LEFT = 0, PS2_TEXT_CENTER = 1, PS2_TEXT_RIGHT = 2 };

float ps2d_text_width(const ps2_draw* d, const char* s, float scale);
/* x is the left edge, centre or right edge by `align`; y the top of the line. */
void ps2d_text(ps2_draw* d, const char* s, float x, float y, float scale, uint32_t argb, int align);
/* Same, cut with ".." so it is at most max_w pixels wide. */
void ps2d_text_fit(ps2_draw* d, const char* s, float x, float y, float scale, uint32_t argb, int align, float max_w);

/* ---- Icon models ------------------------------------------------------------------------ */

typedef struct ps2_model_view {
    float cx, cy;      /* screen position of the model's centre */
    float scale;       /* pixels per model unit */
    float yaw, pitch;  /* radians */
    float roll;        /* radians, turn about the view axis (applied last) */
    float y_offset;    /* model-space shift applied before rotating (centres a model that stands on y = 0) */
    uint32_t tint;     /* ARGB multiplier */
    int tex;           /* texture index, PS2_TEX_ART + slot, or PS2_TEX_NONE */
    int planar_uv;     /* box models: map the whole texture on every face (game pictures) */
    float spec;        /* strength of the highlight on shiny parts (0 = none) */
} ps2_model_view;

/* Draw model `model` (pack index). Returns the number of triangles sent. */
int ps2d_model(ps2_draw* d, int model, const ps2_model_view* v);

#ifdef __cplusplus
}
#endif

#endif /* PS2_DRAW_H */
