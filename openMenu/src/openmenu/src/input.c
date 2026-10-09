/* Controller input for the launcher, see input.h. */
#include <stddef.h>
#include <stdint.h>

#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "input.h"

#define DPAD_MASK (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)

button_t
input_poll(void) {
    static uint32_t prev = 0;
    static int held = 0;
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t* st = dev ? (cont_state_t*)maple_dev_status(dev) : NULL;
    uint32_t now = st ? st->buttons : 0;
    uint32_t edge = now & ~prev;
    button_t out = BTN_NONE;

    if (edge & CONT_A) {
        out = BTN_A;
    } else if (edge & CONT_B) {
        out = BTN_B;
    } else if (edge & CONT_X) {
        out = BTN_X;
    } else if (edge & CONT_START) {
        out = BTN_START;
    } else if (now & DPAD_MASK) {
        if ((edge & DPAD_MASK) || (held > 24 && held % 5 == 0)) {
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
        held++;
    } else {
        held = 0;
    }

    prev = now;
    return out;
}

uint32_t
input_buttons(void) {
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t* st = dev ? (cont_state_t*)maple_dev_status(dev) : NULL;
    return st ? st->buttons : 0;
}
