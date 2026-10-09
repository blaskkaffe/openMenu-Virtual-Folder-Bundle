/*
 * ui_settings: the options of the launcher that matter for booting games, as a flat list
 * of "label: value" rows. Logic only; drawing is up to the front end.
 */
#pragma once

#include <stddef.h>

#include "input.h"

int uis_count(void);

/* Text of row `i`, e.g. "Boot animation: Full". */
void uis_text(int i, char* out, size_t size);

/* Change row `i` by `delta` steps (wraps). */
void uis_change(int i, int delta);

/* Write the changes to the save file. Returns 0 on success. */
int uis_commit(void);
