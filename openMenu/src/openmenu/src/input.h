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
    BTN_Y,
    BTN_START,
    BTN_PAGE_UP,  /* keyboard Page Up / mouse wheel with shift: a page of rows */
    BTN_PAGE_DOWN
} button_t;

/* Poll every controller, mouse and keyboard in every port, whichever are plugged in (any of them steers the menu; several mice move the one pointer); one logical button
 * per call. Keyboard: arrows are the d-pad, Enter is A, Escape and Backspace are B, Tab is X, Page
 * Up/Down page the list. Mouse: left button is A, right button is B, the wheel is up/down. D-pad
 * directions repeat while held (0.4 s, then every 5 frames); buttons fire once per press. Call once
 * per frame. */
button_t input_poll(void);

/* The analog stick that is pushed furthest of all controllers (-128..127 each, 0 in the middle, small deflections read as 0) and the
 * triggers (0..255). All 0 without a controller. */
void input_analog(int* x, int* y, int* ltrig, int* rtrig);

/* Raw button mask of all controllers together (CONT_* bits), for debug overlays. */
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
    int left;    /* the left button is held (dragging) */
    int clicked; /* this frame's button event came from a mouse click (where: x, y) */
    int wheel;   /* this frame's button event came from the wheel: -1 turned away from the user (up), 1 towards (down) */
} mouse_t;

void input_mouse(mouse_t* out);
