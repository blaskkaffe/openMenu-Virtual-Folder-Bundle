/* Adapted from the BIOS-menu decompile project (background/dcbg.h). Constants and maths
 * are the reconstructed ones; no ROM data is contained here. */
/*
 * dcbg.h - Dreamcast BIOS menu background (reference implementation in C).
 *
 * Portable: no platform code.  Produces, every 60 Hz frame, the screen-space
 * vertices the BIOS hands to the PVR (640x480 space, 1/w, u, v, ARGB), so a
 * KallistiOS/openMenu build can submit them unchanged as TR triangle strips,
 * and a PC build can feed them to OpenGL.  See dcbg.js for the same maths in JS
 * and README.md for where each constant comes from in the ROM.
 */
#ifndef DCBG_H
#define DCBG_H
#include <stdint.h>

#define DCBG_MAX_COLS 15
#define DCBG_MAX_ROWS 15

typedef struct {
    float sx, sy, invw, u, v;
    uint32_t argb;                    /* PVR packed colour */
} dcbg_vertex;

typedef struct {
    int cols, rows;
    /* rest */
    float rx[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)], ry[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    float rr[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    uint16_t ra[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    float ru[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)], rv[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    /* live */
    float x[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)], y[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    float z[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    float u[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)], v[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    uint32_t argb[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    int tex;                          /* 7 = ROM clouds, 666 = EXTRA.BG.PVR, 0 = untextured */
} dcbg_mesh;

typedef struct {
    float pos[3], scl[3];
    int rx, rz, rz_step;              /* Ninja angles, 0x10000 = 360 deg */
} dcbg_obj;

typedef struct {
    int custom, hidden;               /* EXTRA.BG constants / teal hidden mode */
    int frame;
    dcbg_mesh swirl, water;           /* objects 0x120 and 0x121 */
    dcbg_obj swirl_obj, water_obj;
    int sw_var6, sw_var7, sw_intro, sw_phase;
    int wa_var0, wa_var6, wa_var7, wa_intro, wa_phase;
} dcbg_state;

void dcbg_init(dcbg_state *s, int custom_texture, int hidden_mode);
void dcbg_step(dcbg_state *s);        /* advance exactly one 60 Hz frame */
void dcbg_fade_out(dcbg_state *s);    /* var7 = 1: fade the layers away */

/* Gradient for the PVR background plane (0xFFRRGGBB). */
void dcbg_gradient(const dcbg_state *s, uint32_t *top, uint32_t *bottom);

/* Project one mesh. out must hold (cols+1)*(rows+1) vertices, indexed i*(rows+1)+j.
 * Draw strips i = 0..cols-1: (i,0),(i+1,0),(i,1),(i+1,1),...,(i,rows),(i+1,rows). */
int dcbg_project(const dcbg_mesh *m, const dcbg_obj *o, dcbg_vertex *out);

#endif
