/*
 * ui_iblue: the Controllers screen (Files icon): pick a port, change what is plugged in, remap
 * every output with press-to-learn or a list, save or discard. Talks to the iBlueControlMod
 * through iblue_link. Draws with the gfx text/rectangle calls, returns sounds instead of playing
 * them, so it runs in the host test with stubs.
 */
#pragma once

#include "iblue_link.h"
#include "input.h"

typedef enum { UIB_SND_NONE, UIB_SND_CURSOR, UIB_SND_CONFIRM, UIB_SND_ENTER, UIB_SND_CANCEL, UIB_SND_ERROR } uib_sound;

/* Open the screen: look for the board and read the ports. Always returns; if the board is not
 * there the screen says so. */
void uib_open(const iblue_io* io);

/* One button press (from input_poll). */
uib_sound uib_button(button_t b);

/* Once per frame, also when no button was pressed (press-to-learn polls the controller). */
void uib_tick(void);

void uib_draw(void);

/* 1 when the user left the screen (changes saved or discarded). */
int uib_done(void);

/* Text for a short message, or "" */
const char* uib_notice(void);
