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

int uis_count(void);

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

/* Rows with more than two choices open a popup. */
int uis_opens_popup(int row);

/* Step to the next/previous choice (wraps). */
void uis_change(int row, int delta);

/* Write the changes to the save file. Returns 0 on success. */
int uis_commit(void);
