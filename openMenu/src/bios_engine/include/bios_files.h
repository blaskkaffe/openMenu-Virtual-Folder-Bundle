/*
 * bios_files: the memory card grid of the BIOS File screen: the four controller ports A-D side by
 * side, two memory card sockets (1 and 2) under each, the memory card icon at the top left and the
 * BACK marker. Which cards are present is told by the caller. Portable C; the texts (title line,
 * free blocks) are drawn by the caller at the positions given here.
 */
#ifndef BIOS_FILES_H
#define BIOS_FILES_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BFILES_SLOTS 8 /* slot = port * 2 + socket - 1: A1 A2 B1 B2 C1 C2 D1 D2 */

typedef struct bfiles {
    bmenu* m;
    int cursor;
    int present[BFILES_SLOTS];
} bfiles;

/* Replace the scene by the card grid with the cursor on `cursor`. */
void bfiles_open(bfiles* f, bmenu* m, int cursor);

/* Move the cursor over the 4x2 grid: dx steps between ports, dy between the two sockets. */
int bfiles_move(bfiles* f, int dx, int dy);
int bfiles_set_cursor(bfiles* f, int slot);

/* Light the selected card and fade the empty sockets; call every frame after bmenu_update(). */
void bfiles_sync(bfiles* f);
void bfiles_draw(bfiles* f, const bscene_sink* sink);

/* Screen position (pixels) of the centre of a card, of the title line and of the small plate at the
 * top left (centre), for the caller's texts. */
void bfiles_card_px(int slot, float* x, float* y);
void bfiles_title_px(float* x, float* y);
void bfiles_plate_px(float* x, float* y);
int bfiles_slot_at_px(float x, float y); /* -1 if none */

#ifdef __cplusplus
}
#endif

#endif /* BIOS_FILES_H */
