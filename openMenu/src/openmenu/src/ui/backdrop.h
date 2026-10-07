/*
 * File: backdrop.h
 * Project: ui
 * Animated 3D backdrops for Folders themes, drawn in the opaque pass behind a see-through background (THEME.INI backdrop=)
 */

#pragma once

#include <stdint.h>

enum backdrop { BACKDROP_NONE = 0, BACKDROP_WAVES, BACKDROP_WAVES_LOW, BACKDROP_SYNTHWAVE };

/* Colors of the synthwave scene: sky, horizon, sun, grid (THEME.INI scene_sky, scene_horizon, scene_sun, scene_grid) */
#define BACKDROP_SCENE_COLORS 4

/* low_res uses fewer triangles */
void backdrop_waves_draw(int low_res);
void backdrop_synthwave_draw(const uint32_t* colors);
