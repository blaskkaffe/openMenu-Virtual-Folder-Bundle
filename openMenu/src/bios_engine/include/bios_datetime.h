/*
 * bios_datetime: the BIOS date and time editor. A panel with the date written out, a green arrow
 * above and below the field being edited (left/right moves between the five fields, up/down
 * changes the value), and the Select and Cancel buttons. Same value ranges and order of fields as
 * the original: years 1950-2085, the order follows the region (year first in Japan, day first in
 * Europe, month first in America). Portable C; the caller draws the panel and the texts.
 */
#ifndef BIOS_DATETIME_H
#define BIOS_DATETIME_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BDT_YEAR_MIN 1950
#define BDT_YEAR_MAX 2085

enum { BDT_YEAR, BDT_MONTH, BDT_DAY, BDT_HOUR, BDT_MINUTE, BDT_SELECT, BDT_CANCEL };
enum { BDT_ORDER_YMD, BDT_ORDER_MDY, BDT_ORDER_DMY };

typedef struct bdt {
    bmenu* m;
    int year, month, day, hour, minute;
    int order;  /* BDT_ORDER_* */
    int cursor; /* BDT_YEAR..BDT_CANCEL */
} bdt;

/* Replace the scene by the editor, starting from this date. */
void bdt_open(bdt* d, bmenu* m, int order, int year, int month, int day, int hour, int minute);

/* Left/right: next field (the right end continues to Select, then Cancel). Returns 1 if it moved. */
int bdt_move(bdt* d, int dx);
/* Up/down: change the field by one (+1 up) on a date field, or switch between Select and Cancel. */
int bdt_change(bdt* d, int dy);

int bdt_days_in_month(int year, int month);

/* "MM/DD/YYYY HH:MM" in the order of the region. */
void bdt_format(const bdt* d, char* out, size_t size);

/* Screen x (pixels) of the centre of a field, 0 for the buttons. The date text must be drawn so
 * that the fields line up with these (it is `BDT_CHAR_W` px per character). */
#define BDT_CHAR_W 12
float bdt_field_center_px(const bdt* d, int field);

/* Left edge (pixels) of the date text so that its fields sit under the arrows. */
float bdt_text_x(const bdt* d);

/* Place the arrows and buttons; call every frame after bmenu_update(). */
void bdt_sync(bdt* d);
void bdt_draw(bdt* d, const bscene_sink* sink);

/* Layout (pixels) for the caller's panel and text. */
#define BDT_PANEL_X 82.0f
#define BDT_PANEL_Y 97.0f
#define BDT_PANEL_W 476.0f
#define BDT_PANEL_H 286.0f
#define BDT_TEXT_Y 283.0f   /* top of the date line */
#define BDT_BUTTON_X 413.0f /* centres of the Select (y 268) and Cancel (y 321) buttons */
#define BDT_SELECT_Y 268.0f
#define BDT_CANCEL_Y 321.0f

#ifdef __cplusplus
}
#endif

#endif /* BIOS_DATETIME_H */
