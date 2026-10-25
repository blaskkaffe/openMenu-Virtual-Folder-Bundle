/*
 * serial_vmu: openMenu 1.7.1's Serial VMU. A serial SD adapter on the console keeps a 128 KB VMU image per game
 * (/sd/OPENMENU/SAVES/<serial>/SLOT1.VMU, TITLE.TXT next to it). Before a game starts the image is written to the
 * VMU that Settings > Serial VMU names; /sd/OPENMENU/LASTDISC.TXT then says which game ran, and the next time the
 * menu starts the VMU is copied back to the SD card. The file layout is the same as 1.7.1's, so both menus can share
 * one card. With Serial VMU Slots on (five images per game, SLOT1..5.VMU) the restore asks which slot (one question, listing each slot's time
or its SLOT<n>.TXT label); the backup at the next start goes into the slot the game was started from, without asking. Not here yet: the fallback VMU picker and exit to BIOS.
 *
 * No drawing: a small state machine that is stepped once per frame (one VMU block a frame, as 1.7.1) and asks its
 * questions as a list of choices, which the front end shows as a popup.
 */
#pragma once

#include <backend/gd_item.h>

typedef enum svmu_phase {
    SVMU_OFF = 0,       /* nothing is going on */
    SVMU_BUSY,          /* copying: svmu_message() says what, svmu_progress() how far */
    SVMU_ASK,           /* a question: svmu_message() and the choices */
    SVMU_LAUNCH,        /* done, start the game (the restore is recorded for the backup at the next start) */
    SVMU_LAUNCH_PLAIN,  /* start the game without any Serial VMU record */
    SVMU_CLOSE          /* done or cancelled: nothing to start */
} svmu_phase;

/* The VMU slot Settings > Serial VMU names, as the File screen numbers them (0 A1, 1 A2, 2 B1 ... 7 D2), -1 when it is off. */
int svmu_slot(void);

/* Serial VMU is on, and this game uses it (it has a serial and is not homebrew or a tool: type "other"). */
int svmu_applies(const gd_item* item);

/* Before a game starts. Afterwards svmu_phase() says what to do. */
void svmu_begin_restore(const gd_item* item);
/* At start: 1 if the last game left a backup to make (the flow has started), else 0. */
int svmu_begin_backup(void);

void svmu_step(void); /* once per frame */
svmu_phase svmu_phase_get(void);
const char* svmu_title(void);
const char* svmu_message(void);
int svmu_progress(void); /* 0..100 while busy */
int svmu_choice_count(void);
const char* svmu_choice_text(int index);
void svmu_choose(int index);
void svmu_cancel(void); /* B: the last-resort choice of the question */
void svmu_finish(void); /* the front end has acted on SVMU_LAUNCH / SVMU_LAUNCH_PLAIN / SVMU_CLOSE */
