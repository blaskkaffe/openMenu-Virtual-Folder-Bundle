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

#define BSCENE_MODEL_CACHE BIOS_MODEL_COUNT
#define BSCENE_MOTION_CACHE 16

typedef struct bscene {
    const bios_rom* rom;
    nj_object models[BSCENE_MODEL_CACHE];
    uint8_t model_state[BSCENE_MODEL_CACHE]; /* 0 not loaded, 1 loaded, 2 failed */
    struct {
        int model, motion;
        int state;
        nj_motion data;
    } motions[BSCENE_MOTION_CACHE];
    int motion_count;
    nj_mat4 pose[256]; /* scratch: world matrices of the object being drawn */
} bscene;

void bscene_init(bscene* s, const bios_rom* rom);
void bscene_free(bscene* s);

/* Draw all objects of `vm` in priority order. */
void bscene_draw_objects(bscene* s, const bvm* vm, const bscene_sink* sink);
void bscene_draw_object(bscene* s, const bvm_obj* obj, const bscene_sink* sink);

/* The animated cloud layers behind the menu. */
void bscene_draw_background(const dcbg_state* bg, const bscene_sink* sink);

/* Screen position of a world point under the BIOS camera. Returns 0 if it is behind the camera. */
int bscene_project(nj_vec3 p, float* sx, float* sy, float* invw);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_SCENE_H */
