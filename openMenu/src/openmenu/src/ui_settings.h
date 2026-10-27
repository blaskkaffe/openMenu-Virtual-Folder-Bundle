/*
 * ui_settings: the launcher's settings as a registry of rows. Logic only; the front end draws
 * them as a scrollable list (bios_page) and shows the choices of a row in a popup.
 *
 * A row is a choice of one of a few named values. Rows with two values toggle when selected;
 * rows with more open a popup menu with their values. Each row belongs to a group (shown in the
 * help box) and has a one-line help text.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* uis_count rows are the Settings list. The Internet screen's Settings page shows uis_net_count rows from uis_net_first on, with the
 * same calls (label, value, uis_change, uis_commit). */
int uis_count(void);
int uis_net_first(void);
int uis_net_count(void);

const char* uis_label(int row);
const char* uis_group(int row);
const char* uis_help(int row);

/* Choices of a row */
int uis_choice_count(int row);
const char* uis_choice_name(int row, int choice);
int uis_get(int row);
void uis_set(int row, int choice);

/* Name of the current value */
const char* uis_value(int row);

/* Rows that are not a choice but open something: the date and time editor, the About box. */
enum { UIS_ACTION_NONE, UIS_ACTION_DATETIME, UIS_ACTION_ABOUT, UIS_ACTION_VMUCLOCK, UIS_ACTION_BENCH };

/* Run an action that does its work at once (the memory card clock); the result shows as the row value. */
void uis_run(int row);
int uis_action(int row);

/* Icon of a row: UIS_ICON_*, or -1 for a placeholder digit. The numbers are those of BPAGE_ICON_* in bios_page.h (a row's icon is
 * handed to the page as it is). Which kind of setting gets which icon: clock and date, anything with a time: the clock; sound: the
 * speaker; memory card, VMU and saving: the VMU; the BIOS language: the lips; the rest: the question mark or none yet. */
enum {
    UIS_ICON_LANGUAGE = 0, /* the lips */
    UIS_ICON_CLOCK = 1,
    UIS_ICON_SOUND = 2,    /* the speaker */
    UIS_ICON_OTHER = 3,    /* the question mark */
    UIS_ICON_VMU = 20,
    UIS_ICON_PAD = 21,     /* the controller */
    UIS_ICON_GLOBE = 22,
    UIS_ICON_PHONE = 23
};
int uis_icon(int row);

/* Rows with more than two choices open a popup. */
int uis_opens_popup(int row);

/* Step to the next/previous choice (wraps). */
void uis_change(int row, int delta);

/* Write the changes to the save file. Returns 0 on success. */
int uis_commit(void);

/* The clock format as the Settings row shows it (0 12 hour, 1 24 hour, 2 date only, 3 12 hour + .beat, 4 24 hour + .beat), and the move of an
 * older build's .beat choice out of the shared save file. */
int uis_clock_mode(void);
void uis_clock_migrate(void);

/* Settings from the [BIOSMENU] section of /cd/DEFAULTS.INI when "Card defaults" is on (see ui_settings.c). Call once at
 * start, after the save file was read. Returns how many were taken. */
int uis_apply_card_defaults(void);

/* The menu background: the BIOS gradient (top and bottom, ARGB) changed by the Background and Dark mode rows. */
void uis_background(uint32_t* top, uint32_t* bottom);
