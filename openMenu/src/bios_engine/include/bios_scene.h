/*
 * bios_scene: turns the state of the BIOS menu engine (script objects, background)
 * into screen-space triangles. It does no drawing itself: a backend (the PVR on the
 * console, a software rasteriser in the PC preview tool) receives the triangles
 * through a sink. Portable C.
 *
 * Projection is the one the BIOS uses: the camera looks down -Z with a screen
 * distance of 4000 on a 640x480 screen.
 */
#ifndef BIOS_SCENE_H
#define BIOS_SCENE_H

#include <stdint.h>

#include "bios_rom.h"
#include "bios_vm.h"
#include "dcbg.h"
#include "bios_models.h"
#include "nj_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSCENE_SCREEN_W 640.0f
#define BSCENE_SCREEN_H 480.0f
#define BSCENE_FOCAL 4000.0f

typedef struct bscene_vtx {
    float x, y;  /* screen pixels */
    float invw;  /* 1/w, what the PVR wants as Z */
    float u, v;
    uint32_t argb;
} bscene_vtx;

typedef enum bscene_tex_kind {
    BSCENE_TEX_NONE = 0,
    BSCENE_TEX_GBIX = 1,    /* a = global index (the background clouds use 7) */
    BSCENE_TEX_TEXLIST = 2  /* a = texlist, b = slot within it */
} bscene_tex_kind;

typedef struct bscene_texref {
    int kind;
    int a, b;
} bscene_texref;

typedef struct bscene_sink {
    void* user;
    /* One alpha blended triangle. */
    void (*triangle)(void* user, const bscene_vtx v[3], bscene_texref tex);
    /* A text surface of object `obj` (text_w x text_h pixels) centred on (x, y). */
    void (*text)(void* user, const bvm_obj* obj, float x, float y, float invw);
} bscene_sink;

/* What bscene_draw_object() draws of an object. Text goes in a pass of its own after all the
 * models, so no sprite ends up under a model that is submitted later. */
#define BSCENE_PART_MODEL 1u
#define BSCENE_PART_TEXT 2u
#define BSCENE_PART_ALL (BSCENE_PART_MODEL | BSCENE_PART_TEXT)

#define BSCENE_MODEL_CACHE BIOS_MODEL_COUNT
#define BSCENE_MOTION_CACHE 16

typedef struct bscene {
    const bios_rom* rom;
    nj_object models[BSCENE_MODEL_CACHE];
    uint8_t model_state[BSCENE_MODEL_CACHE]; /* 0 not loaded, 1 loaded, 2 failed */
    nj_object custom[BMODEL_COUNT];          /* the built-in models of bios_models.h (ids from BMODEL_BASE) */
    uint8_t custom_state[BMODEL_COUNT];
    struct {
        int model, motion;
        int state;
        nj_motion data;
    } motions[BSCENE_MOTION_CACHE];
    int motion_count;
    nj_mat4 pose[256]; /* scratch: world matrices of the object being drawn */
    /* Shorten a model along x without squeezing its ends: model x in [a, b] is compressed by f,
     * what lies beyond b moves along (the rounded ends of a row bar keep their shape). */
    int stretch_on;
    float stretch_a, stretch_b, stretch_f;
    int no_decals; /* skip the polygons of texture 0: the pictures on the CD buttons */
    /* window panel mode (see bscene_draw_panel): the four corners of model 39 are moved apart */
    int panel_on;
    float panel_fx, panel_fy;
    uint32_t panel_accent;
    int fullbright; /* no shading by the light (the gold reverse of a disc looks shinier) */
    int double_alpha;  /* draw objects as if two identical layers were stacked (see bmenu_draw) */
    unsigned parts;    /* BSCENE_PART_* drawn by bscene_draw_object() */
} bscene;

void bscene_init(bscene* s, const bios_rom* rom);
void bscene_free(bscene* s);

/* Draw all objects of `vm` in priority order. */
void bscene_draw_objects(bscene* s, const bvm* vm, const bscene_sink* sink);
void bscene_draw_object(bscene* s, const bvm_obj* obj, const bscene_sink* sink);

/* The animated cloud layers behind the menu. */
/* The BIOS window panel: model 39 with its four corners moved apart to cover the pixel rectangle
 * x, y, w, h (as it looks at `z`, usually BSCENE_PANEL_Z). The rim takes the accent colour
 * (0xAARRGGBB; the screens use orange 0xFFE07000, green 0xFF00E070, blue 0xFF0070E0, magenta
 * 0xFFE00070 and light grey 0xFFE0E0E0), the body is dark and translucent. Drawn like any object:
 * what is submitted afterwards goes on top. */
#define BSCENE_PANEL_Z (-335.9375f)
#define BSCENE_PANEL_MODEL 39
void bscene_draw_panel(bscene* s, float x, float y, float w, float h, uint32_t accent, const bscene_sink* sink);

void bscene_draw_background(const dcbg_state* bg, const bscene_sink* sink);

/* Screen position of a world point under the BIOS camera. Returns 0 if it is behind the camera. */
int bscene_project(nj_vec3 p, float* sx, float* sy, float* invw);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_SCENE_H */
