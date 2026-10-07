/*
 * File: backdrop.h
 * Project: ui
 * The animated 3D backdrops of Folders themes. A backdrop is a scene that draws itself every frame into the opaque pass, behind the
 * see-through background picture of the theme. Each scene is one source file (backdrop_<name>.c) with one function; backdrop.c picks the
 * scene the theme asks for (THEME.INI backdrop_scene=) and gives it the settings from the theme. To add a scene: write the file, add it
 * to CMakeLists.txt, add its name to BACKDROP_SCENES and its function to backdrop_draw().
 */

#pragma once

#include <stdint.h>

typedef enum backdrop_scene {
    BACKDROP_WAVES = 0, /* the DreamPi web page's wave plane and cloud cylinder */
    BACKDROP_SYNTHWAVE, /* a neon grid and mountains moving towards the screen, a striped sun */
    BACKDROP_SCENE_COUNT
} backdrop_scene_t;

/* What a theme tells a backdrop (all from THEME.INI; zero means the scene's own default). Colours are 0xRRGGBB. */
typedef struct backdrop_params {
    backdrop_scene_t scene;
    int low_res;        /* backdrop=2: fewer triangles */
    int clouds_percent; /* waves: the cloud cylinder's opacity in percent of the page's */
    uint32_t tint;      /* waves: a colour the scene is multiplied by (0 = none) */
    int speed_percent;  /* synthwave: how fast the landscape comes towards the screen (100 = normal) */
    int peaks_percent;  /* synthwave: the height of the mountains (100 = normal) */
    uint32_t sky_top, sky_bottom, sun_top, sun_bottom, grid, ground, mountain; /* synthwave palette */
} backdrop_params_t;

/* The scene a THEME.INI backdrop_scene= value names ("waves", "synthwave"); unknown names give the waves. */
backdrop_scene_t backdrop_scene_from_name(const char* name);

/* Draws one frame of the scene the params ask for (call it in the opaque pass). */
void backdrop_draw(const backdrop_params_t* params);

void backdrop_waves_draw(const backdrop_params_t* params);
void backdrop_synthwave_draw(const backdrop_params_t* params);
