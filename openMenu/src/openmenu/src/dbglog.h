/* dbglog: the performance numbers of every screen and debug mode, collected while the debug overlay is on and shown
 * on one page (a table to photograph, then a QR code with the same numbers as text). */
#pragma once

#include <stddef.h>

typedef struct dbglog_sample {
    unsigned fps, tris;
    unsigned sync, build, wait, gpu, ta; /* tenths of a millisecond per frame */
    unsigned vtx, hdr, autosort;
    unsigned beg, bg, drawn; /* tenths of a millisecond: begin frame, background, everything drawn */
} dbglog_sample;

/* One second of numbers for the screen/mode `label`. The first second after the label changes is dropped (the mode
 * has not settled); later seconds replace the last ones, the lowest fps is kept. */
void dbglog_add(const char* label, const dbglog_sample* s);
void dbglog_clear(void);
int dbglog_count(void);

/* The two strings of a table row (up to 42 characters each). */
void dbglog_row(int i, char* left, char* right, size_t cap);

/* All entries as text for the QR code. Returns its length. */
int dbglog_text(char* out, size_t cap, const char* info);
