/* iblue_map: see iblue_map.h. */
#include <stdio.h>
#include <string.h>

#include "iblue_map.h"

/* Generic ids (BlueRetro adapter.h): sticks 0-7, d-pad 8-11, right pad 12-15, face 16-19,
 * menu 20-23, left 24-27, right 28-31. */
static const struct {
    int id;
    const char* name;
} outputs[IBLUE_OUTPUTS] = {
    {18, "A"},          {17, "B"},        {16, "X"},          {19, "Y"},
    {20, "Start"},      {21, "D button"}, {25, "Z"},          {29, "C"},
    {24, "L trigger"},  {28, "R trigger"}, {11, "D-pad up"},  {10, "D-pad down"},
    {8, "D-pad left"},  {9, "D-pad right"}, {3, "Stick up"},  {2, "Stick down"},
    {0, "Stick left"},  {1, "Stick right"}, {15, "Pad 2 up"}, {14, "Pad 2 down"},
    {12, "Pad 2 left"}, {13, "Pad 2 right"}, {7, "Stick 2 up"}, {6, "Stick 2 down"},
};

static const char* const source_names[32] = {
    "L stick left", "L stick right", "L stick down", "L stick up",
    "R stick left", "R stick right", "R stick down", "R stick up",
    "D-pad left", "D-pad right", "D-pad down", "D-pad up",
    "R pad left", "R pad right", "R pad down", "R pad up",
    "Face left", "Face right", "Face down", "Face up",
    "Start", "Select", "Home", "Share",
    "L2 trigger", "L1 bumper", "L misc", "L3 click",
    "R2 trigger", "R1 bumper", "R misc", "R3 click",
};

int
iblue_output_id(int i) {
    return i >= 0 && i < IBLUE_OUTPUTS ? outputs[i].id : -1;
}

const char*
iblue_output_name(int i) {
    return i >= 0 && i < IBLUE_OUTPUTS ? outputs[i].name : "?";
}

int
iblue_output_index(int dst_btn) {
    for (int i = 0; i < IBLUE_OUTPUTS; i++) {
        if (outputs[i].id == dst_btn) {
            return i;
        }
    }
    return -1;
}

const char*
iblue_source_name(int src_btn, char* buf, int buf_len) {
    if (src_btn >= 0 && src_btn < 32) {
        return source_names[src_btn];
    }
    snprintf(buf, (size_t)buf_len, "Key/mouse %d", src_btn);
    return buf;
}

int
iblue_sources_of(const iblue_map_entry* map, int size, int port, int out, int* ids, int max) {
    int id = iblue_output_id(out), n = 0;
    for (int i = 0; i < size && n < max; i++) {
        if (map[i].dst_btn == id && map[i].dst_id == port) {
            ids[n++] = map[i].src_btn;
        }
    }
    return n;
}

static void
widen(int i, int* lo, int* hi) {
    if (*hi < 0) {
        *lo = *hi = i;
    } else {
        if (i < *lo) *lo = i;
        if (i > *hi) *hi = i;
    }
}

int
iblue_assign(iblue_map_entry* map, int* size, int port, int src, int out, int* first, int* count) {
    int id = iblue_output_id(out), lo = -1, hi = -1, own = -1, old_dst = IBLUE_DST_NONE;

    *first = *count = 0;
    if (id < 0) {
        return -1;
    }
    for (int i = 0; i < *size; i++) {
        if (map[i].src_btn == src && map[i].dst_id == port) {
            own = i;
            break;
        }
    }
    if (own < 0) {
        if (*size >= IBLUE_MAP_MAX) {
            return -1;
        }
        own = (*size)++;
        memset(&map[own], 0, sizeof(map[own]));
        map[own].src_btn = (uint8_t)src;
        map[own].dst_btn = IBLUE_DST_NONE;
        map[own].dst_id = (uint8_t)port;
        map[own].perc_max = 100;
        map[own].perc_threshold = 50;
        map[own].perc_deadzone = 135;
        widen(own, &lo, &hi);
    }
    old_dst = map[own].dst_btn;
    if (old_dst == id) {
        if (hi >= 0) {
            *first = lo;
            *count = hi - lo + 1;
        }
        return 0;
    }
    for (int i = 0; i < *size; i++) {
        if (i != own && map[i].dst_btn == id && map[i].dst_id == port) {
            map[i].dst_btn = (uint8_t)old_dst;
            widen(i, &lo, &hi);
        }
    }
    map[own].dst_btn = (uint8_t)id;
    widen(own, &lo, &hi);
    *first = lo;
    *count = hi - lo + 1;
    return 0;
}

void
iblue_clear_output(iblue_map_entry* map, int size, int port, int out, int* first, int* count) {
    int id = iblue_output_id(out), lo = -1, hi = -1;
    for (int i = 0; i < size; i++) {
        if (id >= 0 && map[i].dst_btn == id && map[i].dst_id == port) {
            map[i].dst_btn = IBLUE_DST_NONE;
            widen(i, &lo, &hi);
        }
    }
    *first = hi >= 0 ? lo : 0;
    *count = hi >= 0 ? hi - lo + 1 : 0;
}
