/*
 * brctl - talk to a BlueRetro adapter (iBlueControlMod fork) from Dreamcast
 * software over the Maple bus. KallistiOS.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Typical use:
 *     int port = brctl_find();               // -1 if no BlueRetro answers
 *     brctl_led_rgb(port, 0xFF, 0x16, 0x00, 1);
 *     brctl_pair_start(port);
 *
 * All calls block for at most ~100 ms per Maple frame and return
 * BRCTL_OK (0) or a negative BRCTL_E* code.
 *
 * The channel is only available on ports where BlueRetro currently presents
 * a controller (i.e. a Bluetooth pad is connected there, with hotplug on).
 */

#ifndef _BRCTL_H_
#define _BRCTL_H_

#include <stdint.h>
#include "brctl_proto.h"
#include "brctl_config.h"

#define BRCTL_OK         0
#define BRCTL_ENODEV    -1   /* no device on that port */
#define BRCTL_ETIMEOUT  -2   /* no answer: not a BlueRetro, or frame lost */
#define BRCTL_EREJECT   -3   /* adapter rejected the request (range etc.) */
#define BRCTL_EPROTO    -4   /* unexpected answer / version mismatch */

/* Return the first port (0..3 = A..D) where a BlueRetro answers, else -1 */
int brctl_find(void);

int brctl_get_info(int port, struct brctl_info *info);
int brctl_get_status(int port, struct brctl_status *status);

/* Raw access (chunked internally, offsets/lengths are bytes) */
int brctl_read(int port, uint8_t item, uint32_t offset, void *buf, uint32_t len);
int brctl_write(int port, uint8_t item, uint32_t offset, const void *buf, uint32_t len);
int brctl_op(int port, uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t arg2);

/* An operation that runs later on the adapter (profiles, game config, config save ...): sends it,
 * waits until it has finished (status.op_seq changes, up to about a second) and returns the
 * BRCTL_RES_* of it in *result (may be NULL). Returns BRCTL_OK, or a BRCTL_E* if it never answered. */
int brctl_op_wait(int port, uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t arg2, int *result);

/* Whole BlueRetro config (same struct as blueretro.io) */
int brctl_config_load(int port, struct br_config *cfg);
/* Writes the whole struct and asks the adapter to apply + persist it */
int brctl_config_save(int port, const struct br_config *cfg);

/* Convenience */
static inline int brctl_led_rgb(int port, uint8_t r, uint8_t g, uint8_t b, int save) {
    return brctl_op(port, BRCTL_OP_LED_RGB, ((uint32_t)r << 16) | ((uint32_t)g << 8) | b, save ? 1 : 0, 0);
}
static inline int brctl_led_preset(int port, uint32_t preset) {
    return brctl_op(port, BRCTL_OP_LED_PRESET, preset, 0, 0);
}
static inline int brctl_pair_start(int port) { return brctl_op(port, BRCTL_OP_PAIR_START, 0, 0, 0); }
static inline int brctl_pair_stop(int port) { return brctl_op(port, BRCTL_OP_PAIR_STOP, 0, 0, 0); }
static inline int brctl_disconnect_all(int port) { return brctl_op(port, BRCTL_OP_DISCONNECT_ALL, 0, 0, 0); }
static inline int brctl_config_default(int port) { return brctl_op(port, BRCTL_OP_CONFIG_DEFAULT, 0, 0, 0); }
static inline int brctl_adapter_reset(int port) { return brctl_op(port, BRCTL_OP_ADAPTER_RESET, 0, 0, 0); }

/* Button / touch sensor: choose what each gesture does (BRCTL_ACT_*).
 * BRCTL_ACT_NONE = adapter does nothing, your software reacts to
 * status.btn_seq / btn_last instead. persist=1 keeps it across power cycles. */
static inline int brctl_btn_set(int port, uint32_t gesture, uint32_t action, int persist) {
    return brctl_op(port, BRCTL_OP_BTN_SET, gesture, action, persist ? 1 : 0);
}
static inline int brctl_btn_defaults(int port, int persist) {
    return brctl_op(port, BRCTL_OP_BTN_DEFAULTS, persist ? 1 : 0, 0, 0);
}

#endif /* _BRCTL_H_ */
