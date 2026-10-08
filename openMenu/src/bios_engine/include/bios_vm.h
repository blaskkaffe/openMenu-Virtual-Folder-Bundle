/*
 * bios_vm: the object and script engine of the Dreamcast BIOS menu.
 *
 * Every visual element of the BIOS menu is an object driven by up to four
 * bytecode "tracks" read straight from the ROM's script bank (see bios_rom).
 * This module runs the scripts and the per-object tweens exactly like the
 * original per-frame update (obj_run_scripts_and_draw without the drawing)
 * and leaves the result in plain structs for a renderer to draw.
 *
 * Portable C: no hardware, no allocation. Effects that depend on the console
 * (VMU icons, CD state, text content) are delegated to host callbacks.
 */
#ifndef BIOS_VM_H
#define BIOS_VM_H

#include <stdint.h>

#include "bios_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BVM_MAX_OBJECTS 64
#define BVM_TRACKS 4
#define BVM_VARS 16
#define BVM_STEP_BUDGET 4096 /* instructions one track may run before yielding */

/* Object render flags (same bit positions as the original) */
#define BVM_F_ATTACHED 0x00008 /* position is relative to `parent` */
#define BVM_F_HIDE 0x00010
#define BVM_F_MODEL 0x00100
#define BVM_F_MOTION 0x00200
#define BVM_F_COLOUR 0x00400 /* use `color` as constant material */
#define BVM_F_TEXT 0x00800
#define BVM_F_MESH 0x01000
#define BVM_F_UNSCALE 0x02000
#define BVM_F_PANEL 0x10000
#define BVM_F_FLAG17 0x20000
#define BVM_F_FLAG20 0x100000

typedef struct bvm_track {
    int16_t active, wait;
    uint32_t pc;
    uint32_t loop_ret[4];
    int16_t loop_var[4];
    uint32_t call_ret[4];
    int16_t loop_depth, call_depth;
} bvm_track;

typedef struct bvm_ftween {
    float cur, step, target;
    int16_t frames;
} bvm_ftween;

typedef struct bvm_itween {
    int32_t cur, step, target;
    int16_t frames;
} bvm_itween;

typedef struct bvm_obj {
    int active;
    uint16_t id;
    int16_t prio;
    uint32_t flags;
    struct bvm_obj* parent;
    bvm_track track[BVM_TRACKS];
    int32_t var[BVM_VARS];

    float pos[3]; /* final position, parent offset applied */
    int32_t rot[3];
    float color[4]; /* constant material: a, r, g, b offsets */
    uint16_t model, motion, texlist;
    bvm_ftween pos_tw[3];
    bvm_itween rot_tw[3];
    bvm_ftween scale_tw[3]; /* only .cur is used */
    bvm_ftween motion_tw;   /* current motion frame (.cur), loops at .target back to var[7] */

    /* text surface (ops 0x70..0x7B), recorded for the renderer */
    int text_w, text_h;
    uint16_t text_attr, text_colour, text_bg;
    int text_adv_wide, text_adv_narrow;
    float text_off[3];
    /* grid mesh (ops 0x80..0x85) */
    int mesh_cw, mesh_ch, mesh_cols, mesh_rows;
    int32_t mesh_tex;
    int mesh_mode;
    /* window panel (ops 0x86..0x88) */
    int panel_w, panel_h, rect_w, rect_h;
} bvm_obj;

struct bvm;

typedef struct bvm_host {
    void* user;
    /* Non-zero while the pad shows input (drives the idle/screensaver script op). May be NULL. */
    int (*input_active)(void* user);
    /* Op 0x2B: console specific effects (sub 6 = "language text" and friends). May be NULL. */
    void (*effect)(void* user, struct bvm* vm, bvm_obj* obj, int sub, int arg);
} bvm_host;

typedef struct bvm {
    const bios_rom* rom;
    bvm_host host;
    bvm_obj objs[BVM_MAX_OBJECTS];
    int order[BVM_MAX_OBJECTS]; /* object indices, highest priority first */
    int count;
    int error;                  /* last script error: bad opcode / out of range pc, 0 when fine */
    uint32_t error_pc;
} bvm;

void bvm_init(bvm* vm, const bios_rom* rom, const bvm_host* host);

/* Start script `script` (index in the bank) as a new object. Returns it, NULL if the
 * script is unused or there is no free slot. Mirrors obj_create(). */
bvm_obj* bvm_create(bvm* vm, int script, uint16_t id, int16_t prio);

bvm_obj* bvm_find(bvm* vm, uint16_t id);
void bvm_kill(bvm* vm, uint16_t id); /* all objects with this id */
void bvm_kill_all(bvm* vm);

/* One 60 Hz frame: run every object's scripts and advance its tweens. */
void bvm_update(bvm* vm);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_VM_H */
