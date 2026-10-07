/*
 * File: backdrop_waves.c
 * Project: ui
 * The "waves" 3D backdrop of Folders themes (THEME.INI backdrop_scene=waves, the default): the Dreamcast-BIOS background of the DreamPi
 * web page. One of the scenes in backdrop.h; the others are in their own files.
 */

#include <math.h>
#include <stdint.h>

#include "ui/backdrop.h"
#include "ui/backdrop_texture.h"
#include "ui/draw_prototypes.h"

/* The animated backdrop of themes that ask for it (THEME.INI backdrop=1): the Dreamcast-BIOS-style background of the DreamPi web page.
 *
 * It is the same scene as the page's Three.js one (dc-background.js of DreamPiAutoToggle, adapted from the VMU Icon Maker by Robert Dale
 * Smith, MIT): a camera at (0, -20, 7) with a 75 degree field of view looking along +y, a 50 x 30 plane at z = 0 whose height is four
 * outgoing ripples, fading out with the distance from the middle, and a big cylinder (radius 40, rotating slowly) behind it that fades
 * out downward, both covered with the same 64x64 cloud texture, over a sky gradient. Lighting is the page's: an ambient 0x404040 and two
 * directional lights, worked out per vertex.
 *
 * How it fits the PVR. The scene goes into the OPAQUE list, not the translucent one. The translucent list is where all the text goes, and the
 * PVR keeps a short list of the polygons in each 32 x 32 tile (32 entries); a tile with text in it is already half full, so a scene drawn
 * there made the PVR drop polygons (squares missing from pictures, vanishing text and borders). The page's fade to transparent is done with
 * the colours instead: a vertex gets the picture times (opacity x light) as its colour and (1 - opacity) x the sky behind it as its offset
 * colour, which adds up to the same blend. The meshes also stay near the screen (the page's plane and cylinder run far off it), because the PVR
 * draws polygons with huge coordinates wrongly. The plane is a grid whose rows are spaced evenly on the screen and whose width follows the
 * screen's width at that row (22 x 24 cells); the cylinder is the front half, only the rows that reach the screen (8 rows). Together about
 * 1100 vertices a frame. The texture is 8 KB of video memory, allocated once; if that fails the sky gradient still draws. Colours are exactly
 * the page's unless the theme gives a backdrop_color to tint them.
 *
 * draw_backdrop() (the sky) and draw_backdrop_scene() both go in the opaque pass, in that order. */

#define SCENE_FOCAL     469.15f /* 720 / 2 / tan(37.5 degrees): the page draws the scene on a canvas 240 px taller than a 480 px view, centred on it */
#define SCENE_CAM_Y     (-20.0f)
#define SCENE_CAM_Z     7.0f
#define SCENE_FWD_Y     0.998305f /* the view direction towards (0, 100, 0) from the camera, and the camera's up vector */
#define SCENE_FWD_Z     (-0.058232f)
#define SCENE_UP_Y      0.058232f
#define SCENE_UP_Z      0.998305f
#define PLANE_COLS      24
#define PLANE_ROWS      22
#define CYL_SEGMENTS    16
#define CYL_ROWS        5
#define CYL_FIRST       26.0f /* the part of the cylinder that reaches the screen: from here ... */
#define CYL_LAST        60.0f /* ... to here along its axis (its opacity is nearly zero beyond) */
#define CYL_HEIGHT      70.0f
#define CYL_RADIUS      40.0f

typedef struct scene_vertex {
    float x, y, w, u, v;
    uint32_t argb;  /* the picture's multiplier: opacity x light */
    uint32_t oargb; /* what is added: (1 - opacity) x the sky */
    int ok;
} scene_vertex_t;

static pvr_ptr_t backdrop_txr = NULL;
static int backdrop_txr_tried = 0;
static uint32_t backdrop_tint = 0xFFFFFFFFu;
static uint32_t backdrop_frame = 0;

static int
backdrop_texture(void) {
    if (!backdrop_txr_tried) {
        backdrop_txr_tried = 1;
        backdrop_txr = pvr_mem_malloc(sizeof(backdrop_texture_data));
        if (backdrop_txr != NULL) {
            pvr_txr_load((void*)backdrop_texture_data, backdrop_txr, sizeof(backdrop_texture_data));
        }
    }
    return backdrop_txr != NULL;
}

static uint32_t
tinted(uint32_t argb) {
    if ((backdrop_tint & 0x00FFFFFFu) == 0x00FFFFFFu || (backdrop_tint & 0x00FFFFFFu) == 0) {
        return argb;
    }
    return (argb & 0xFF000000u) | ((((argb >> 16) & 0xFF) * ((backdrop_tint >> 16) & 0xFF) / 255) << 16)
           | ((((argb >> 8) & 0xFF) * ((backdrop_tint >> 8) & 0xFF) / 255) << 8)
           | (((argb & 0xFF) * (backdrop_tint & 0xFF)) / 255);
}

/* The sky gradient at a screen row, as 0..255 floats. */
static void
sky_at(float sy, float* r, float* g, float* b) {
    const float f = sy < 0.0f ? 0.0f : (sy > 480.0f ? 1.0f : sy * (1.0f / 480.0f));

    *r = 142.0f + (90.0f - 142.0f) * f;
    *g = 179.0f + (115.0f - 179.0f) * f;
    *b = 209.0f + (167.0f - 209.0f) * f;
}

/* World point to screen, as the page's camera sees it. */
static int
scene_project(float x, float y, float z, scene_vertex_t* out) {
    const float dy = y - SCENE_CAM_Y;
    const float dz = z - SCENE_CAM_Z;
    const float depth = SCENE_FWD_Y * dy + SCENE_FWD_Z * dz;
    float inv;

    if (depth < 2.0f) {
        out->ok = 0;
        return 0;
    }
    inv = 1.0f / depth;
    out->x = 320.0f + SCENE_FOCAL * x * inv;
    out->y = 240.0f - SCENE_FOCAL * (SCENE_UP_Y * dy + SCENE_UP_Z * dz) * inv;
    out->w = inv;
    out->ok = 1;
    return 1;
}

/* Sets a vertex's colours for an opacity and a light level. */
static void
scene_colour(scene_vertex_t* v, float opacity, float light) {
    float sr, sg, sb;
    const int base = (int)(255.0f * light * opacity);
    const float rest = 1.0f - opacity;

    sky_at(v->y, &sr, &sg, &sb);
    v->argb = tinted(0xFF000000u | ((uint32_t)base << 16) | ((uint32_t)base << 8) | (uint32_t)base);
    v->oargb = tinted(0xFF000000u | ((uint32_t)(sr * rest) << 16) | ((uint32_t)(sg * rest) << 8) | (uint32_t)(sb * rest));
}

/* The sky: the page's gradient, as it shows on a 480 pixel screen. */
static void
waves_sky(uint32_t tint) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const uint32_t top = tinted(0xFF8EB3D1u);
    const uint32_t bottom = tinted(0xFF5A73A7u);

    backdrop_tint = tint;
    pvr_poly_cxt_col(&context, draw_get_list());
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    pvr_vertex_t vert = {.argb = bottom, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = 0.001f, .u = 0, .v = 0};

    vert.x = 0.0f;
    vert.y = 480.0f;
    pvr_prim(&vert, sizeof(vert));
    vert.argb = top;
    vert.y = 0.0f;
    pvr_prim(&vert, sizeof(vert));
    vert.argb = bottom;
    vert.x = 640.0f;
    vert.y = 480.0f;
    pvr_prim(&vert, sizeof(vert));
    vert.argb = top;
    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.y = 0.0f;
    pvr_prim(&vert, sizeof(vert));
}

static void
scene_header(int clamp) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    pvr_poly_cxt_txr(&context, draw_get_list(), PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED, 64, 64, backdrop_txr, PVR_FILTER_BILINEAR);
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.specular = PVR_SPECULAR_ENABLE; /* the offset colour is added to the picture */
    if (clamp) {
        context.txr.uv_clamp = PVR_UVCLAMP_UV;
    }
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));
}

static void
scene_strip_vertex(const scene_vertex_t* v, int last) {
    pvr_vertex_t vert = {.flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX, .x = v->x, .y = v->y, .z = v->w, .u = v->u, .v = v->v,
                         .argb = v->argb, .oargb = v->oargb};

    pvr_prim(&vert, sizeof(vert));
}

/* The page's ripples: four waves going out from the middle, softened at the very middle and by distance. Returns the height and the
 * opacity. (The page never recomputes the plane's normals, so its lighting stays that of a flat plane; no slopes are needed.) */
static float
plane_height(float x, float y, float t, float* opacity) {
    const float distance = sqrtf(x * x + y * y);
    const float decay = expf(-distance * 0.15f);
    const float centre = distance < 3.0f ? distance * (1.0f / 3.0f) : 1.0f;
    float h = 0.0f;

    for (int i = 0; i < 4; i++) {
        const float amplitude = 0.6f * (0.9f - (float)i * 0.25f);

        h += amplitude * fsin(distance * 0.6f - t + (float)i * 0.19634954f) * decay * centre;
    }
    *opacity = distance < 20.0f ? 1.0f - distance * 0.05f : 0.0f;
    return h;
}

/* The world y that lands on a screen row for a point on the plane (z = 0): (240 - sy) * depth = focal * up, with dz = -7, solved for dy. */
static float
plane_y_at_row(float sy) {
    const float up = 240.0f - sy;
    const float num = 7.0f * (up * SCENE_FWD_Z - SCENE_FOCAL * SCENE_UP_Z);
    const float den = up * SCENE_FWD_Y - SCENE_FOCAL * SCENE_UP_Y;

    return num / den + SCENE_CAM_Y;
}

static void
draw_scene_plane(float t, int cols, int rows) {
    static scene_vertex_t grid[PLANE_ROWS + 1][PLANE_COLS + 1]; /* the largest size; a low-res theme uses less of it */

    for (int j = 0; j <= rows; j++) {
        /* Row 0 is the plane's far edge (y = 15). The others run from just below the horizon to the bottom of the screen, evenly on the
         * screen, so that the rows are about 10 px apart wherever the ripples are. */
        const float row_y = j == 0 ? 15.0f : plane_y_at_row(300.0f + 215.0f * (float)(j - 1) / (rows - 1));
        const float depth = SCENE_FWD_Y * (row_y - SCENE_CAM_Y) + SCENE_FWD_Z * (0.0f - SCENE_CAM_Z);
        /* The width of the screen plus a margin, as plane units at this row, and never more than the part of the plane that shows. */
        float half = 400.0f * depth * (1.0f / SCENE_FOCAL);

        half = half > 20.0f ? 20.0f : half;
        for (int i = 0; i <= cols; i++) {
            const float s = -1.0f + 2.0f * (float)i / cols;
            const float x = (s < 0.0f ? -half : half) * powf(fabsf(s), 1.4f);
            float opacity;
            const float h = plane_height(x, row_y, t, &opacity);
            scene_vertex_t* v = &grid[j][i];

            if (scene_project(x, row_y, h, v)) {
                v->u = (x + 25.0f) * (1.0f / 50.0f);
                v->v = (15.0f - row_y) * (1.0f / 30.0f);
                /* Ambient 0.25 + the light from (1, 1, 1) and the one from (1, -15, 1) on the flat normal (0, 0, 1), and the little
                 * specular the page's material adds: 0.25 + 0.577 + 0.066 + 0.05. */
                scene_colour(v, opacity, 0.94f);
            }
        }
    }

    scene_header(1);
    for (int j = 0; j < rows; j++) {
        for (int i = 0; i <= cols; i++) {
            /* Rows run from the far side to the near side; a strip takes the lower point (nearer) and the upper one. */
            if (!grid[j][i].ok || !grid[j + 1][i].ok) {
                continue;
            }
            scene_strip_vertex(&grid[j + 1][i], 0);
            scene_strip_vertex(&grid[j][i], i == cols);
        }
    }
}

static void
draw_scene_cylinder(uint32_t frame, float clouds) {
    static scene_vertex_t ring[CYL_SEGMENTS + 1][CYL_ROWS + 1];
    const float spin = -0.001f * (float)frame; /* rotation.y of the page's cylinder */
    const float tilt_c = 0.0839f;              /* cos / sin of rotation.x = pi / 0.655 */
    const float tilt_s = -0.9965f;

    for (int k = 0; k <= CYL_SEGMENTS; k++) {
        const float phi = (float)k * (6.2831853f / CYL_SEGMENTS);
        const float xl = CYL_RADIUS * fsin(phi);
        const float zl = CYL_RADIUS * fcos(phi);
        /* The spin about the cylinder's own axis, then the tilt, then the position (0, 0, 65). */
        const float xs = xl * fcos(spin) + zl * fsin(spin);
        const float zs = -xl * fsin(spin) + zl * fcos(spin);

        for (int j = 0; j <= CYL_ROWS; j++) {
            const float yl = CYL_FIRST + (CYL_LAST - CYL_FIRST) * (float)j / CYL_ROWS;
            const float wy = yl * tilt_c - zs * tilt_s;
            const float wz = 65.0f + yl * tilt_s + zs * tilt_c;
            const float n = yl * (1.0f / 60.0f);
            const float t0 = (n + 0.7f) * (1.0f / 1.8f); /* smoothstep(-0.7, 1.1, n) */
            const float tt = t0 < 0.0f ? 0.0f : (t0 > 1.0f ? 1.0f : t0);
            float opacity = (1.0f - tt * tt * (3.0f - 2.0f * tt)) * clouds; /* clouds: 1 = the page's own faint cylinder */
            scene_vertex_t* v = &ring[k][j];

            opacity = opacity > 1.0f ? 1.0f : opacity;

            if (scene_project(xs, wy, wz, v)) {
                /* The page's shader uses the raw texture coordinates (its repeat and offset settings do not reach a shader), so the
                 * picture goes once around the cylinder, and its top is the top of the column. */
                v->u = 0.5f - fabsf(0.5f - (float)k / CYL_SEGMENTS); /* half the picture, mirrored on the other half: no seam */
                v->v = 1.0f - yl * (1.0f / CYL_HEIGHT);
                scene_colour(v, opacity, 1.0f);
            }
        }
    }

    scene_header(1);
    for (int k = 0; k < CYL_SEGMENTS; k++) {
        /* Only the half of the wall in front of the camera. */
        const float mid = ((float)k + 0.5f) * (6.2831853f / CYL_SEGMENTS);
        const float cx = CYL_RADIUS * fsin(mid);
        const float cz = CYL_RADIUS * fcos(mid);
        const float wy = -cx * fsin(spin) + cz * fcos(spin);

        if (wy < -8.0f) {
            continue;
        }
        for (int j = 0; j <= CYL_ROWS; j++) {
            if (!ring[k][j].ok || !ring[k + 1][j].ok) {
                continue;
            }
            scene_strip_vertex(&ring[k][j], 0);
            scene_strip_vertex(&ring[k + 1][j], j == CYL_ROWS);
        }
    }
}

static void
waves_scene(int low_res, int clouds_percent) {
    const float t = (float)backdrop_frame * 0.016f; /* the page adds 0.016 a frame */

    backdrop_frame++;
    if (!backdrop_texture()) {
        return;
    }
    draw_scene_cylinder(backdrop_frame, clouds_percent > 0 ? (float)clouds_percent * 0.01f : 1.0f);
    /* 24 x 22 cells normally, 16 x 16 for a low-res theme (THEME.INI backdrop=2): about half the triangles */
    draw_scene_plane(t, low_res ? 16 : PLANE_COLS, low_res ? 16 : PLANE_ROWS);
}

void
backdrop_waves_draw(const backdrop_params_t* p) {
    waves_sky(p->tint);
    waves_scene(p->low_res, p->clouds_percent);
}
