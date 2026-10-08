/*
 * gfx: PVR backend for the BIOS-style menu. Uploads ROM textures on demand, draws
 * the triangles produced by bios_scene, and renders text with the BIOS font into
 * small textures.
 */
#pragma once

#include <stdint.h>

#include <bios_rom.h>
#include <bios_scene.h>

/* Initialise the PVR. `rom` may be NULL (then only text and flat colours work). */
int gfx_init(const bios_rom* rom);

/* One frame: gradient first, then everything drawn through gfx_sink()/gfx_text()
 * goes into the translucent list. */
void gfx_begin_frame(uint32_t top_argb, uint32_t bottom_argb);
void gfx_end_frame(void);

const bscene_sink* gfx_sink(void);

/* Triangles submitted since gfx_begin_frame(). */
unsigned gfx_triangles(void);

/* Text of the label objects (icon names, header...) by script object id. */
void gfx_set_label(uint16_t obj_id, const char* text);

/* Draw `str` with its top-left corner at (x, y). Strings are cut to 42 characters.
 * `shadow` draws a dark copy 2 px down-right first. */
void gfx_text(const char* str, float x, float y, float z, uint32_t argb, int shadow);

/* Flat translucent rectangle. */
void gfx_rect(float x, float y, float w, float h, float z, uint32_t argb);

#define GFX_CHAR_W 12
#define GFX_LINE_H 32
