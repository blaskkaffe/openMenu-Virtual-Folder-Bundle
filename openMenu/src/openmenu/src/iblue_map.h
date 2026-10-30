/*
 * iblue_map: what the remap screen knows about the iBlue's mapping table: names of the
 * Dreamcast outputs and of the controller's source buttons, and the "assign" rule.
 * Logic only (no console code).
 *
 * The table has one entry per source button (src_btn): pressing that source acts as dst_btn
 * on port dst_id. A destination that is not a Dreamcast output (IBLUE_DST_NONE) means the source
 * does nothing. Ids are BlueRetro's generic button ids.
 */
#pragma once

#include "iblue_link.h"

#define IBLUE_OUTPUTS 24

/* Generic id of the Dreamcast output `i` (0..IBLUE_OUTPUTS-1) and its name. */
int iblue_output_id(int i);
const char* iblue_output_name(int i);
/* Index of the output with this generic id, -1 if the id is not a Dreamcast output. */
int iblue_output_index(int dst_btn);

/* Name of a source button (generic id 0-31: a gamepad; above: keyboard / mouse). */
const char* iblue_source_name(int src_btn, char* buf, int buf_len);

/* Source buttons that act as output `out` on `port`: fills ids (at most max), returns the count. */
int iblue_sources_of(const iblue_map_entry* map, int size, int port, int out, int* ids, int max);

/*
 * Make `src` act as output `out` on `port`. The output that `src` had until now goes to the
 * sources that had `out` (a swap); with no output before, they do nothing. A source without an
 * entry gets one added when there is room. The changed range is returned in *first / *count
 * (count 0 = nothing changed); *size may grow. Returns 0, or -1 if the table is full.
 */
int iblue_assign(iblue_map_entry* map, int* size, int port, int src, int out, int* first, int* count);

/* Make the sources of output `out` do nothing. Changed range as above. */
void iblue_clear_output(iblue_map_entry* map, int size, int port, int out, int* first, int* count);
