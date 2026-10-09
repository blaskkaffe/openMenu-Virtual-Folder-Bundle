/* Controller input for the launcher: one logical button per frame. */
#pragma once

#include <stdint.h>

typedef enum {
    BTN_NONE,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_A,
    BTN_B,
    BTN_X,
    BTN_START,
    BTN_PAGE_UP,  /* keyboard Page Up / mouse wheel with shift: a page of rows */
    BTN_PAGE_DOWN
} button_t;

/* Poll the controller on port A, a mouse and a keyboard, whichever are plugged in; one logical button
 * per call. Keyboard: arrows are the d-pad, Enter is A, Escape and Backspace are B, Tab is X, Page
 * Up/Down page the list. Mouse: left button is A, right button is B, the wheel is up/down. D-pad
 * directions repeat while held (0.4 s, then every 5 frames); buttons fire once per press. Call once
 * per frame. */
button_t input_poll(void);

/* Raw button mask of controller port A (CONT_* bits), for debug overlays. */
uint32_t input_buttons(void);

/* A letter or digit typed on the keyboard since the last call ('A'..'Z', '0'..'9'), 0 if none. Used
 * to jump to a game by its first letter. */
int input_typed_char(void);

/* The mouse: a pointer position kept inside the 640x480 screen. */
typedef struct {
    int present; /* a mouse is plugged in */
    int visible; /* it has moved lately: draw the pointer */
    int x, y;
    int moved;   /* moved since the last call */
} mouse_t;

void input_mouse(mouse_t* out);
