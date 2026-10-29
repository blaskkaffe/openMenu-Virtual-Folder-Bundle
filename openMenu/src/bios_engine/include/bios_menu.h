/*
 * bios_menu: the BIOS main menu (four 3D icons on the cloud background) as a
 * portable state machine on top of bios_vm / bios_scene / dcbg. It only knows
 * how to create the menu objects, move the selection and advance one frame;
 * what the icons *do* is up to the caller. Portable C.
 */
#ifndef BIOS_MENU_H
#define BIOS_MENU_H

#include "bios_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMENU_ICONS 4

typedef enum bmenu_dir { BMENU_UP, BMENU_DOWN, BMENU_LEFT, BMENU_RIGHT } bmenu_dir;

/* Object ids of the original menu */
#define BMENU_ID_ICON(i) (0x200 + (i))
#define BMENU_ID_CAPTION(i) (0x300 + (i))
#define BMENU_ID_HEADER 0x1FF

/* Accent colours of the window panels, by main menu item (set_panel_accent_color in the BIOS):
 * the same colours as the pills behind the icon names on the main screen. */
#define BMENU_ACCENT_PLAY 0xFFE07000u     /* orange */
#define BMENU_ACCENT_FILE 0xFF00E070u    /* green */
#define BMENU_ACCENT_MUSIC 0xFF0070E0u    /* blue: Music, and the online category that takes its place */
#define BMENU_ACCENT_SETTINGS 0xFFE00070u /* magenta */
#define BMENU_ACCENT_MAIN 0xFFE0E0E0u     /* light grey: popups on the main screen */

/* The header bar is model 7; its texture slot 0 is the "Dreamcast" logo (a 128x32 picture). */
#define BMENU_HEADER_MODEL 7
#define BMENU_LOGO_SLOT 0

typedef struct bmenu {
    const bios_rom* rom;
    bvm vm;
    bscene scene;
    dcbg_state bg;
    int selected;
    int frames;
    /* 1 when the translucent polygons are sorted per pixel by the PVR (autosort, as the BIOS draws its menu):
     * the objects are then sent in their own order. 0 (presort): they go through the CPU sorter instead,
     * far to near by triangle, which is close but not exact where the parts of a model overlap. */
    int hw_autosort;
    /* A main menu icon drawn with another model (the Music icon is a globe while the Internet is on): model 0 = the script's own. The
     * model has no motion of its own, so it turns while its icon is selected. */
    struct {
        uint16_t model;
        float scale, dx, dy, dz; /* size, and where the model's middle is moved to (world units; dz > 0 away from the viewer, behind the caption) */
        int selected_frames;
    } icon_model[BMENU_ICONS];
} bmenu;

void bmenu_set_icon_model(bmenu* m, int icon, uint16_t model, float scale, float dx, float dy, float dz);

void bmenu_init(bmenu* m, const bios_rom* rom, const bvm_host* host);
void bmenu_free(bmenu* m);

/* (Re)create the main menu objects with icon `selected` highlighted. */
void bmenu_show_main(bmenu* m, int selected);

/* Select icon `i` directly (the mouse). Returns 1 if the selection changed. */
int bmenu_select(bmenu* m, int i);

/* D-pad on the 2x2 icon grid. Returns 1 if the selection changed. */
int bmenu_move(bmenu* m, bmenu_dir dir);

/* The BACK marker (object 0x1110, script 6) as the BIOS script effect (0x8C021CD0) colours it: the arrow and the
 * frame only light up while it is selected, and the frame blinks yellow for 16 frames and dark for 16. `anim` counts
 * frames; call before drawing. `pal` picks the arrow colour of the PAL console. */
void bmenu_back_style(bmenu* m, int selected, int anim, int pal);
/* Is the pixel (px, py) on a BACK marker placed at (x, y) world units? */
int bmenu_back_hit(float x, float y, float px, float py);

/* The colours of the caption pill of main menu icon `icon` (0 Play, 1 File, 2 Music, 3 Settings), read from the ROM's pill model: the
 * body and the lighter border, as 0xAARRGGBB with the alpha the BIOS gives them. Returns 0 if the ROM has no such model (the
 * accent colours are given then). */
int bmenu_pill_colours(bmenu* m, int icon, uint32_t* body, uint32_t* border);

void bmenu_update(bmenu* m);                          /* one 60 Hz frame */
void bmenu_draw(bmenu* m, const bscene_sink* sink);   /* background layers, then all objects */

/* The objects only, every one of them as the BIOS draws it: each icon twice (objects 0x200+i and 0x300+i, two layers of
 * about 60% make the 84% seen on screen). See hw_autosort for the order they are sent in. */
void bmenu_draw_objects(bmenu* m, const bscene_sink* sink);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_MENU_H */
