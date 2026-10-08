/* Controller input for the launcher: one logical button per frame. */
#pragma once

#include <stdint.h>

typedef enum { BTN_NONE, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_START } button_t;

/* Poll controller port A. D-pad directions repeat while held (0.4 s, then every 5 frames);
 * buttons fire once per press. Call once per frame. */
button_t input_poll(void);

/* Raw button mask of controller port A (CONT_* bits), for debug overlays. */
uint32_t input_buttons(void);
