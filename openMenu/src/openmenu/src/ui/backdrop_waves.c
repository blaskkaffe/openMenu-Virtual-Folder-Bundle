/*
 * File: backdrop_waves.c
 * Project: ui
 * Waves 3D backdrop: the Dreamcast BIOS style background of the DreamPi web page (THEME.INI backdrop=waves)
 * Same scene as the page's Three.js one (adapted from the VMU Icon Maker by Robert Dale Smith, MIT): a plane with
 * four outgoing ripples and a slowly turning cloud cylinder behind it, both fading out, over a sky gradient
 */

#include <math.h>
#include <stdint.h>

#include "ui/backdrop.h"
#include "ui/backdrop_texture.h"
#include "ui/draw_prototypes.h"

#define FOCAL        469.15f /* the page draws the scene on a canvas 240 px taller than the screen */
#define CAM_Y        (-20.0f)
#define CAM_Z        7.0f
#define FWD_Y        0.998305f /* view direction and up vector of the camera */
#define FWD_Z        (-0.058232f)
#define UP_Y         0.058232f
#define UP_Z         0.998305f
#define PLANE_COLS   16
#define PLANE_ROWS   16
#define CYL_SEGMENTS 16
#define CYL_ROWS     5
#define CYL_FIRST    26.0f /* the part of the cylinder that reaches the screen */
#define CYL_LAST     60.0f
#define CYL_HEIGHT   70.0f
#define CYL_RADIUS   40.0f
#define CLOUDS       2.5f /* opacity of the cylinder, 1 is the page's very faint one */

/* The fade to transparent is done with the colors, so the scene can stay in the opaque pass: the picture is multiplied by
 * (opacity x light) and (1 - opacity) x the sky behind it is added as the offset color */
typedef struct {
    float x, y, w, u, v;
    uint32_t argb, oargb;
} vertex_t;

static pvr_ptr_t texture = NULL;
static int texture_tried = 0;
static uint32_t frame = 0;

static int
load_texture(void) {
    if (!texture_tried) {
        texture_tried = 1;
        texture = pvr_mem_malloc(sizeof(backdrop_texture_data));
        if (texture != NULL) {
            pvr_txr_load((void*)backdrop_texture_data, texture, sizeof(backdrop_texture_data));
        }
    }
    return texture != NULL;
}

/* Projects a world point to the screen as the page's camera sees it, w is 1 / depth */
static void
project(float x, float y, float z, vertex_t* out) {
    const float dy = y - CAM_Y;
    const float dz = z - CAM_Z;
    const float inv = 1.0f / (FWD_Y * dy + FWD_Z * dz);

    out->x = 320.0f + FOCAL * x * inv;
    out->y = 240.0f - FOCAL * (UP_Y * dy + UP_Z * dz) * inv;
    out->w = inv;
}

/* Sets the colors of a vertex for an opacity and a light level */
static void
set_color(vertex_t* v, float opacity, float light) {
    const float f = v->y < 0.0f ? 0.0f : (v->y > 480.0f ? 1.0f : v->y / 480.0f);
    const int base = (int)(255.0f * light * opacity);
    const float rest = 1.0f - opacity;

    v->argb = 0xFF000000u | ((uint32_t)base << 16) | ((uint32_t)base << 8) | (uint32_t)base;
    v->oargb = 0xFF000000u | ((uint32_t)((142.0f - 52.0f * f) * rest) << 16) | ((uint32_t)((179.0f - 64.0f * f) * rest) << 8)
               | (uint32_t)((209.0f - 42.0f * f) * rest);
}

static void
strip_vertex(const vertex_t* v, int last) {
    pvr_vertex_t vert = {.flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX, .x = v->x, .y = v->y, .z = v->w, .u = v->u, .v = v->v,
                         .argb = v->argb, .oargb = v->oargb};

    pvr_prim(&vert, sizeof(vert));
}

static void
texture_header(int clamp) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    pvr_poly_cxt_txr(&context, draw_get_list(), PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED, 64, 64, texture, PVR_FILTER_BILINEAR);
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.specular = PVR_SPECULAR_ENABLE;
    if (clamp) {
        context.txr.uv_clamp = PVR_UVCLAMP_UV;
    }
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));
}

static void
draw_sky(void) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const uint32_t top = 0xFF8EB3D1u, bottom = 0xFF5A73A7u;
    pvr_vertex_t vert = {.argb = bottom, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = 0.001f, .u = 0, .v = 0};

    pvr_poly_cxt_col(&context, draw_get_list());
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

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

/* Height of the plane at x, y: four ripples going out from the middle, softer by distance. Also returns the opacity */
static float
plane_height(float x, float y, float t, float* opacity) {
    const float distance = sqrtf(x * x + y * y);
    const float decay = expf(-distance * 0.15f);
    const float centre = distance < 3.0f ? distance / 3.0f : 1.0f;
    float h = 0.0f;

    for (int i = 0; i < 4; i++) {
        h += 0.6f * (0.9f - (float)i * 0.25f) * fsin(distance * 0.6f - t + (float)i * 0.19634954f) * decay * centre;
    }
    *opacity = distance < 20.0f ? 1.0f - distance * 0.05f : 0.0f;
    return h;
}

/* The world y that lands on a screen row for a point on the plane (z = 0) */
static float
plane_y_at_row(float sy) {
    const float up = 240.0f - sy;

    return 7.0f * (up * FWD_Z - FOCAL * UP_Z) / (up * FWD_Y - FOCAL * UP_Y) + CAM_Y;
}

/* The rows of the plane are spaced evenly on the screen and its width follows the screen's, so no polygon is far off it */
static void
draw_plane(float t) {
    static vertex_t grid[PLANE_ROWS + 1][PLANE_COLS + 1];

    for (int j = 0; j <= PLANE_ROWS; j++) {
        const float row_y = j == 0 ? 15.0f : plane_y_at_row(300.0f + 215.0f * (float)(j - 1) / (PLANE_ROWS - 1));
        const float depth = FWD_Y * (row_y - CAM_Y) - FWD_Z * CAM_Z;
        const float half = 400.0f * depth / FOCAL > 20.0f ? 20.0f : 400.0f * depth / FOCAL;

        for (int i = 0; i <= PLANE_COLS; i++) {
            const float s = -1.0f + 2.0f * (float)i / PLANE_COLS;
            const float x = (s < 0.0f ? -half : half) * powf(fabsf(s), 1.4f);
            float opacity;
            const float h = plane_height(x, row_y, t, &opacity);
            vertex_t* v = &grid[j][i];

            project(x, row_y, h, v);
            v->u = (x + 25.0f) / 50.0f;
            v->v = (15.0f - row_y) / 30.0f;
            set_color(v, opacity, 0.94f); /* the page's lighting on a flat plane */
        }
    }

    texture_header(1);
    for (int j = 0; j < PLANE_ROWS; j++) {
        for (int i = 0; i <= PLANE_COLS; i++) {
            strip_vertex(&grid[j + 1][i], 0);
            strip_vertex(&grid[j][i], i == PLANE_COLS);
        }
    }
}

/* Only the half of the cylinder in front of the camera, and only its rows that reach the screen */
static void
draw_cylinder(uint32_t spin_frame) {
    static vertex_t ring[CYL_SEGMENTS + 1][CYL_ROWS + 1];
    const float spin = -0.001f * (float)spin_frame;
    const float tilt_c = 0.0839f, tilt_s = -0.9965f;

    for (int k = 0; k <= CYL_SEGMENTS; k++) {
        const float phi = (float)k * (6.2831853f / CYL_SEGMENTS);
        const float xl = CYL_RADIUS * fsin(phi);
        const float zl = CYL_RADIUS * fcos(phi);
        const float xs = xl * fcos(spin) + zl * fsin(spin);
        const float zs = -xl * fsin(spin) + zl * fcos(spin);

        for (int j = 0; j <= CYL_ROWS; j++) {
            const float yl = CYL_FIRST + (CYL_LAST - CYL_FIRST) * (float)j / CYL_ROWS;
            const float t0 = (yl / 60.0f + 0.7f) / 1.8f;
            const float t = t0 < 0.0f ? 0.0f : (t0 > 1.0f ? 1.0f : t0);
            float opacity = (1.0f - t * t * (3.0f - 2.0f * t)) * CLOUDS; /* smoothstep fade out along the cylinder */
            vertex_t* v = &ring[k][j];

            opacity = opacity > 1.0f ? 1.0f : opacity;
            project(xs, yl * tilt_c - zs * tilt_s, 65.0f + yl * tilt_s + zs * tilt_c, v);
            v->u = (float)k / CYL_SEGMENTS; /* the picture goes once around */
            v->v = 1.0f - yl / CYL_HEIGHT;
            set_color(v, opacity, 1.0f);
        }
    }

    texture_header(0);
    for (int k = 0; k < CYL_SEGMENTS; k++) {
        const float mid = ((float)k + 0.5f) * (6.2831853f / CYL_SEGMENTS);

        if (-CYL_RADIUS * fsin(mid) * fsin(spin) + CYL_RADIUS * fcos(mid) * fcos(spin) < -8.0f) {
            continue;
        }
        for (int j = 0; j <= CYL_ROWS; j++) {
            strip_vertex(&ring[k][j], 0);
            strip_vertex(&ring[k + 1][j], j == CYL_ROWS);
        }
    }
}

void
backdrop_waves_draw(void) {
    draw_sky();
    if (load_texture()) {
        draw_cylinder(++frame);
        draw_plane((float)frame * 0.016f);
    }
}
