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

typedef struct bmenu {
    const bios_rom* rom;
    bvm vm;
    bscene scene;
    dcbg_state bg;
    int selected;
    int frames;
} bmenu;

void bmenu_init(bmenu* m, const bios_rom* rom, const bvm_host* host);
void bmenu_free(bmenu* m);

/* (Re)create the main menu objects with icon `selected` highlighted. */
void bmenu_show_main(bmenu* m, int selected);

/* D-pad on the 2x2 icon grid. Returns 1 if the selection changed. */
int bmenu_move(bmenu* m, bmenu_dir dir);

void bmenu_update(bmenu* m);                          /* one 60 Hz frame */
void bmenu_draw(bmenu* m, const bscene_sink* sink);   /* background layers, then all objects */

#ifdef __cplusplus
}
#endif

#endif /* BIOS_MENU_H */
