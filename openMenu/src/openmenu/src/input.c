/* Controller, keyboard and mouse input for the launcher, see input.h. */
#include <stddef.h>
#include <stdint.h>

#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/keyboard.h>
#include <dc/maple/mouse.h>

#include "input.h"

#define DPAD_MASK (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)

/* USB HID usage codes, which is what the keyboard driver queues */
#define KEY_A 0x04
#define KEY_Z 0x1D
#define KEY_1 0x1E
#define KEY_0 0x27
#define KEY_ENTER 0x28
#define KEY_ESCAPE 0x29
#define KEY_BACKSPACE 0x2A
#define KEY_TAB 0x2B
#define KEY_SPACE 0x2C
#define KEY_PGUP 0x4B
#define KEY_PGDOWN 0x4E
#define KEY_RIGHT 0x4F
#define KEY_LEFT 0x50
#define KEY_DOWN 0x51
#define KEY_UP 0x52

#define POINTER_HIDE_FRAMES 600 /* the pointer disappears after about ten seconds without moving */

#define PORTS 4 /* controllers, mice and keyboards of every port are read; their state is kept by the port */
#define MAX_DEVICES 8 /* enumerated devices of one kind (a hub or a combined device can show more than four) */

static int typed_char;
static mouse_t mouse = {0, 0, 320, 240, 0, 0, 0, 0};
static int mouse_idle;
static uint32_t mouse_prev_buttons[PORTS];

static int
port_of(const maple_device_t* dev) {
    return dev->port >= 0 && dev->port < PORTS ? dev->port : 0;
}

/* One controller: its buttons since the last frame. */
static button_t
pad_one(maple_device_t* dev) {
    static uint32_t prev[PORTS];
    static int held[PORTS];
    int p = port_of(dev);
    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    uint32_t now = st ? st->buttons : 0;
    uint32_t edge = now & ~prev[p];
    button_t out = BTN_NONE;

    if (edge & CONT_A) {
        out = BTN_A;
    } else if (edge & CONT_B) {
        out = BTN_B;
    } else if (edge & CONT_X) {
        out = BTN_X;
    } else if (edge & CONT_Y) {
        out = BTN_Y;
    } else if (edge & CONT_START) {
        out = BTN_START;
    } else if (now & DPAD_MASK) {
        if ((edge & DPAD_MASK) || (held[p] > 24 && held[p] % 5 == 0)) {
            if (now & CONT_DPAD_UP) {
                out = BTN_UP;
            } else if (now & CONT_DPAD_DOWN) {
                out = BTN_DOWN;
            } else if (now & CONT_DPAD_LEFT) {
                out = BTN_LEFT;
            } else {
                out = BTN_RIGHT;
            }
        }
        held[p]++;
    } else {
        held[p] = 0;
    }

    prev[p] = now;
    return out;
}

/* Every controller in every port steers the menu; each one is read every frame (its edges and repeat belong to it), the first
 * button of the frame is the answer. */
static button_t
pad_poll(void) {
    button_t out = BTN_NONE;
    for (int i = 0; i < MAX_DEVICES; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
        if (!dev) {
            break;
        }
        button_t b = pad_one(dev);
        if (out == BTN_NONE) {
            out = b;
        }
    }
    return out;
}

/* Move the pointer; a button press or the wheel turns into a button event. Every mouse in every port moves the one pointer. */
static button_t
mouse_poll(void) {
    int present = 0, dx = 0, dy = 0, dz = 0;
    uint32_t held_buttons = 0, edge = 0;
    button_t out = BTN_NONE;

    for (int i = 0; i < MAX_DEVICES; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_MOUSE);
        mouse_state_t* st = dev ? (mouse_state_t*)maple_dev_status(dev) : NULL;
        if (!dev) {
            break;
        }
        if (!st) {
            continue;
        }
        int p = port_of(dev);
        present = 1;
        dx += st->dx;
        dy += st->dy;
        dz += st->dz;
        held_buttons |= st->buttons;
        edge |= st->buttons & ~mouse_prev_buttons[p];
        mouse_prev_buttons[p] = st->buttons;
    }

    mouse.present = present;
    mouse.moved = 0;
    mouse.clicked = 0;
    mouse.wheel = 0;
    if (!present) {
        mouse.visible = 0;
        mouse.left = 0;
        return BTN_NONE;
    }
    mouse.left = (held_buttons & MOUSE_LEFTBUTTON) != 0;
    if (dx || dy) {
        mouse.x += dx;
        mouse.y += dy;
        mouse.x = mouse.x < 0 ? 0 : (mouse.x > 639 ? 639 : mouse.x);
        mouse.y = mouse.y < 0 ? 0 : (mouse.y > 479 ? 479 : mouse.y);
        mouse.moved = 1;
        mouse.visible = 1;
        mouse_idle = 0;
    } else if (mouse.visible && ++mouse_idle > POINTER_HIDE_FRAMES) {
        mouse.visible = 0;
    }

    if (edge & MOUSE_LEFTBUTTON) {
        out = BTN_A;
        mouse.clicked = 1;
    } else if (edge & MOUSE_RIGHTBUTTON) {
        out = BTN_B;
        mouse.clicked = 1;
    } else if (dz < 0) {
        out = BTN_UP; /* the wheel turned away from the user */
        mouse.wheel = -1;
    } else if (dz > 0) {
        out = BTN_DOWN;
        mouse.wheel = 1;
    }
    if (out != BTN_NONE) {
        mouse.visible = 1;
        mouse_idle = 0;
    }
    return out;
}

static button_t
key_to_button(int key) {
    if (key >= KEY_A && key <= KEY_Z) {
        typed_char = 'A' + (key - KEY_A);
        return BTN_NONE;
    }
    if (key >= KEY_1 && key <= KEY_0) {
        typed_char = key == KEY_0 ? '0' : '1' + (key - KEY_1);
        return BTN_NONE;
    }
    switch (key) {
        case KEY_UP: return BTN_UP;
        case KEY_DOWN: return BTN_DOWN;
        case KEY_LEFT: return BTN_LEFT;
        case KEY_RIGHT: return BTN_RIGHT;
        case KEY_ENTER: return BTN_A;
        case KEY_ESCAPE:
        case KEY_BACKSPACE: return BTN_B;
        case KEY_TAB: return BTN_X;
        case KEY_SPACE: return BTN_Y;
        case KEY_PGUP: return BTN_PAGE_UP;
        case KEY_PGDOWN: return BTN_PAGE_DOWN;
        default: return BTN_NONE;
    }
}

/* Every keyboard in every port: the next key of the first one that has one. Keys of the others wait in their queues. */
static button_t
keyboard_poll(void) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_KEYBOARD);
        if (!dev) {
            break;
        }
        int key = kbd_queue_pop(dev, 0); /* the raw key code, -1 when nothing was pressed */
        if (key < 0) {
            continue;
        }
        button_t b = key_to_button(key);
        if (b != BTN_NONE || typed_char) {
            return b;
        }
    }
    return BTN_NONE;
}

button_t
input_poll(void) {
    button_t out = pad_poll();
    button_t m = mouse_poll(); /* always run: it keeps the pointer position */
    if (out == BTN_NONE) {
        out = m;
    } else {
        mouse.clicked = 0; /* the event is the controller's */
        mouse.wheel = 0;
    }
    if (out == BTN_NONE) {
        out = keyboard_poll();
    }
    return out;
}

int
input_typed_char(void) {
    int c = typed_char;
    typed_char = 0;
    return c;
}

void
input_mouse(mouse_t* out) {
    *out = mouse;
}

void
input_analog(int* x, int* y, int* ltrig, int* rtrig) {
    int bx = 0, by = 0, lt = 0, rt = 0, best = -1;
    for (int i = 0; i < MAX_DEVICES; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
        if (!dev) {
            break;
        }
        cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
        if (!st) {
            continue;
        }
        int jx = st->joyx > -12 && st->joyx < 12 ? 0 : st->joyx; /* a resting stick is seldom exactly in the middle */
        int jy = st->joyy > -12 && st->joyy < 12 ? 0 : st->joyy;
        int mag = (jx < 0 ? -jx : jx) + (jy < 0 ? -jy : jy);
        if (mag > best) { /* the stick that is pushed furthest */
            best = mag;
            bx = jx;
            by = jy;
        }
        lt = st->ltrig > lt ? st->ltrig : lt;
        rt = st->rtrig > rt ? st->rtrig : rt;
    }
    *x = bx;
    *y = by;
    *ltrig = lt;
    *rtrig = rt;
}

uint32_t
input_buttons(void) {
    uint32_t all = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
        if (!dev) {
            break;
        }
        cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
        all |= st ? st->buttons : 0;
    }
    return all;
}
