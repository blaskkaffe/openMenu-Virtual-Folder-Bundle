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

/* Replace the "Dreamcast" logo of the header bar by a .PVR file (GBIX optional, 16-bit
 * twiddled / rectangle / VQ, no mipmaps). The picture is stretched over the logo's area,
 * which is 4:1: 128x32 is the natural size. Returns 0 if the file was found and used;
 * otherwise the BIOS logo stays. */
int gfx_load_logo(const char* path);

/* Triangles submitted since gfx_begin_frame(). */
unsigned gfx_triangles(void);

/* Text of the label objects (icon names, header...) by script object id. */
void gfx_set_label(uint16_t obj_id, const char* text);

/* Draw `str` with its top-left corner at (x, y). Strings are cut to 42 characters.
 * `shadow` is ignored: the BIOS glyphs carry their own shade. */
void gfx_text(const char* str, float x, float y, float z, uint32_t argb, int shadow);

/* Flat translucent rectangle, plain or with rounded corners of radius r. */
void gfx_rrect(float x, float y, float w, float h, float r, float z, uint32_t argb);
void gfx_rect(float x, float y, float w, float h, float z, uint32_t argb);

#define GFX_CHAR_W 11 /* BTEXT_ADVANCE: pixels per character (a space is 8) */

/* Width in pixels of a string as gfx_text() draws it. */
int gfx_text_width(const char* str);
#define GFX_LINE_H 32

/* Game art from ICON.DAT / BOX.DAT. Row discs: bind the product of each visible row (slot as in
 * bios_list) and the renderer puts its icon on the disc label. */
void gfx_art_bind_row(int slot, const char* product, int pal);
/* Box art of a game as a textured rectangle (a blank frame while it loads or if there is none). */
void gfx_art_box(const char* product, float x, float y, float w, float h, float z);

/* The front picture of the built-in models of bios_models.h (the jewel cases): box art of `product`,
 * or plain white when empty or not found. The back picture is white until something is placed there. */
void gfx_model_bind_front(const char* product);

/* Game list row `slot`: show its title through a window of `window_px` pixels, moved left by
 * `offset_px` (a scrolling title). window_px 0 shows the title as it is. */
void gfx_set_row_scroll(int slot, int window_px, int offset_px);

/* 16:9 mode: everything but the background gradient is squeezed to 3/4 of its width around the
 * centre of the screen, so a wide TV that stretches the picture shows it in proportion. */
void gfx_set_aspect(int wide);

/* Small pictures made at run time, e.g. the icons of files on a memory card. gfx_dyn_create() copies a
 * w x h ARGB4444 picture into video memory and returns its id (-1 if there is no room); free them when
 * the screen that uses them is left. gfx_image() draws one as a rectangle. */
int gfx_dyn_create(const unsigned short* argb4444, int w, int h);
void gfx_dyn_free(int id);
void gfx_dyn_free_all(void);
void gfx_image(int id, float x, float y, float w, float h, float z);
