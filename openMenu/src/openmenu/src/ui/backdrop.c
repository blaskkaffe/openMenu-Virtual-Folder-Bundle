/*
 * File: backdrop.c
 * Project: ui
 * Picks the 3D backdrop scene a Folders theme asks for. See backdrop.h.
 */

#include <stddef.h>
#include <strings.h>

#include "ui/backdrop.h"

static const char* const BACKDROP_SCENES[BACKDROP_SCENE_COUNT] = {"waves", "synthwave"};

backdrop_scene_t
backdrop_scene_from_name(const char* name) {
    if (name != NULL) {
        for (int i = 0; i < BACKDROP_SCENE_COUNT; i++) {
            if (strcasecmp(name, BACKDROP_SCENES[i]) == 0) {
                return (backdrop_scene_t)i;
            }
        }
    }
    return BACKDROP_WAVES;
}

void
backdrop_draw(const backdrop_params_t* params) {
    switch (params->scene) {
        case BACKDROP_SYNTHWAVE: backdrop_synthwave_draw(params); break;
        case BACKDROP_WAVES:
        default: backdrop_waves_draw(params); break;
    }
}
