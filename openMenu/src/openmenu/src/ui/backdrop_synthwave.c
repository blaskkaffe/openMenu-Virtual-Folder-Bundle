/*
 * File: backdrop_synthwave.c
 * Project: ui
 * The "synthwave" 3D backdrop of Folders themes (THEME.INI backdrop_scene=synthwave): a neon grid floor that comes towards the screen,
 * mountains on both sides that rise and fall a little as they pass, and a striped sun on the horizon. One of the scenes in backdrop.h.
 *
 * Everything is flat or Gouraud coloured polygons in the opaque pass (like the waves scene, so the translucent pass keeps its room for the
 * text), a few hundred quads a frame: the sky, the sun's stripes, the floor, 20 grid lines across it and 25 along it, and two mountain
 * meshes of 12 x 7 cells with a thin bright line on every row. Depth is 1 / distance, so nearer things cover farther ones. Coordinates
 * are kept near the screen (the PVR draws polygons with huge coordinates wrongly): the mountain meshes end just beyond the screen's
 * edge, and the grid lines the same.
 *
 * The landscape is a function of the distance travelled (it scrolls) and of the frame count (the mountains breathe), so nothing is stored
 * from frame to frame. Colours come from the theme (THEME.INI scene_sky_top, scene_sky_bottom, scene_sun_top, scene_sun_bottom,
 * scene_grid, scene_ground, scene_mountain); the defaults are a magenta sunset.
 */

#include <math.h>
#include <stdint.h>

#include "ui/backdrop.h"
#include "ui/draw_prototypes.h"

#define SW_HORIZON 236.0f /* screen row of the horizon */
#define SW_FOCAL   420.0f
#define SW_CAM_H   5.0f   /* the camera's height above the floor */
#define SW_Z_NEAR  (SW_FOCAL * SW_CAM_H / (480.0f - SW_HORIZON)) /* the depth that lands on the bottom of the screen */
#define SW_Z_FAR   140.0f
#define SW_Z_LINES 96.0f /* the farthest line across the floor (beyond it they would only pile up at the horizon) */
#define SW_Z_MOUNTAINS 105.0f /* the farthest row of the mountain meshes */
#define SW_STEP    8.0f /* distance between the lines across the floor */
#define SW_LANES   3.2f /* distance between the lines along it */
#define SW_ROAD    9.0f /* the flat part of the floor, either side of the middle */
#define SW_MAX_ROWS 12
#define SW_MAX_COLS 7

typedef struct sw_colour {
    float r, g, b;
} sw_colour_t;

static uint32_t sw_frame = 0;
static float sw_distance = 0.0f;

static sw_colour_t
sw_unpack(uint32_t c, uint32_t fallback) {
    sw_colour_t out;

    c = (c & 0x00FFFFFFu) ? c : fallback;
    out.r = (float)((c >> 16) & 0xFF);
    out.g = (float)((c >> 8) & 0xFF);
    out.b = (float)(c & 0xFF);
    return out;
}

static sw_colour_t
sw_mix(sw_colour_t a, sw_colour_t b, float t) {
    sw_colour_t out;

    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    out.r = a.r + (b.r - a.r) * t;
    out.g = a.g + (b.g - a.g) * t;
    out.b = a.b + (b.b - a.b) * t;
    return out;
}

static uint32_t
sw_pack(sw_colour_t c) {
    const int r = c.r < 0.0f ? 0 : (c.r > 255.0f ? 255 : (int)c.r);
    const int g = c.g < 0.0f ? 0 : (c.g > 255.0f ? 255 : (int)c.g);
    const int b = c.b < 0.0f ? 0 : (c.b > 255.0f ? 255 : (int)c.b);

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void
sw_vertex(float x, float y, float z, uint32_t argb, int last) {
    pvr_vertex_t vert = {.argb = argb, .oargb = 0, .flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX, .x = x, .y = y, .z = z, .u = 0, .v = 0};

    pvr_prim(&vert, sizeof(vert));
}

/* A four cornered polygon as one strip: top left, top right, bottom left, bottom right, each with its own colour and depth. */
static void
sw_quad(float x0, float y0, float z0, uint32_t c0, float x1, float y1, float z1, uint32_t c1, float x2, float y2, float z2, uint32_t c2,
        float x3, float y3, float z3, uint32_t c3) {
    sw_vertex(x0, y0, z0, c0, 0);
    sw_vertex(x1, y1, z1, c1, 0);
    sw_vertex(x2, y2, z2, c2, 0);
    sw_vertex(x3, y3, z3, c3, 1);
}

static float
sw_smooth(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

/* The mountains' height at a floor position (ax: across, away from the middle; zw: the distance along the world, which is the depth
 * plus the distance travelled). The wobble with the frame count is what makes them breathe. */
static float
sw_height(float ax, float zw, float amplitude) {
    const float ramp = sw_smooth((ax - SW_ROAD - 1.0f) * (1.0f / 8.0f));
    const float n = 0.5f * fsin(0.31f * zw + 0.25f * ax) + 0.3f * fsin(0.77f * zw - 0.43f * ax + 1.3f) + 0.2f * fsin(1.41f * zw + 0.7f * ax + 2.1f);
    const float n01 = 0.5f + 0.5f * n;
    const float breath = 1.0f + 0.16f * fsin(0.045f * (float)sw_frame + 0.11f * zw);

    return amplitude * ramp * (0.08f + 0.92f * n01 * n01 * n01) * breath;
}

void
backdrop_synthwave_draw(const backdrop_params_t* p) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const int rows = p->low_res ? 8 : SW_MAX_ROWS;
    const int cols = p->low_res ? 5 : SW_MAX_COLS;
    const float speed = p->speed_percent > 0 ? (float)p->speed_percent * 0.01f : 1.0f;
    const float amplitude = 13.0f * (p->peaks_percent > 0 ? (float)p->peaks_percent * 0.01f : 1.0f);
    const sw_colour_t sky_top = sw_unpack(p->sky_top, 0x14042EU);
    const sw_colour_t sky_bottom = sw_unpack(p->sky_bottom, 0xFF5AA0U);
    const sw_colour_t sun_top = sw_unpack(p->sun_top, 0xFFE23CU);
    const sw_colour_t sun_bottom = sw_unpack(p->sun_bottom, 0xFF2A8CU);
    const sw_colour_t grid = sw_unpack(p->grid, 0xFF3CC8U);
    const sw_colour_t ground = sw_unpack(p->ground, 0x12062AU);
    const sw_colour_t mountain = sw_unpack(p->mountain, 0x2A0C4AU);
    const sw_colour_t haze = sw_mix(sky_bottom, ground, 0.45f);
    const uint32_t black = sw_pack(ground);

    sw_distance += 0.35f * speed; /* about 20 units a second */
    sw_frame++;

    pvr_poly_cxt_col(&context, draw_get_list());
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    /* The sky, from the top of the screen to the horizon. */
    sw_quad(0.0f, 0.0f, 0.0001f, sw_pack(sky_top), 640.0f, 0.0f, 0.0001f, sw_pack(sky_top), 0.0f, SW_HORIZON + 2.0f, 0.0001f,
            sw_pack(sky_bottom), 640.0f, SW_HORIZON + 2.0f, 0.0001f, sw_pack(sky_bottom));

    /* The sun: a disc cut into stripes that get wider towards the horizon, half hidden by the floor. */
    {
        const float radius = 92.0f;
        const float cy = SW_HORIZON - 34.0f;
        const float top = cy - radius;
        const float reach = SW_HORIZON - top;
        float y = top;

        while (y < SW_HORIZON) {
            const float f = (y - top) / reach;
            const float gap = f < 0.35f ? 0.0f : (f - 0.35f) * (1.0f / 0.65f) * 5.0f;
            const float step = f < 0.35f ? 3.0f : 7.0f;
            const float height = step - gap;
            const float yc = y + height * 0.5f - cy;
            const float chord = radius * radius - yc * yc;

            if (chord > 0.0f && height > 0.5f) {
                const float half = sqrtf(chord);
                const uint32_t c0 = sw_pack(sw_mix(sun_top, sun_bottom, f));
                const uint32_t c1 = sw_pack(sw_mix(sun_top, sun_bottom, (y + height - top) / reach));

                sw_quad(320.0f - half, y, 0.0003f, c0, 320.0f + half, y, 0.0003f, c0, 320.0f - half, y + height, 0.0003f, c1, 320.0f + half,
                        y + height, 0.0003f, c1);
            }
            y += step;
        }
    }

    /* The floor. */
    sw_quad(0.0f, SW_HORIZON, 0.001f, sw_pack(sw_mix(ground, haze, 0.55f)), 640.0f, SW_HORIZON, 0.001f, sw_pack(sw_mix(ground, haze, 0.55f)), 0.0f,
            480.0f, 0.001f, black, 640.0f, 480.0f, 0.001f, black);

    /* The lines across the floor, coming towards the screen. */
    {
        const float offset = fmodf(sw_distance, SW_STEP);
        int k = (int)ceilf((SW_Z_NEAR + offset) / SW_STEP);

        for (float z = (float)k * SW_STEP - offset; z <= SW_Z_LINES; z = (float)(++k) * SW_STEP - offset) {
            const float y = SW_HORIZON + SW_FOCAL * SW_CAM_H / z;
            const float thick = SW_FOCAL * 0.07f / z < 1.0f ? 1.0f : SW_FOCAL * 0.07f / z;
            const float fade = 1.0f - sqrtf((z - SW_Z_NEAR) / (SW_Z_FAR - SW_Z_NEAR));
            const uint32_t c = sw_pack(sw_mix(haze, grid, fade));
            const float zz = 1.0f / z;

            sw_quad(0.0f, y, zz, c, 640.0f, y, zz, c, 0.0f, y + thick, zz, c, 640.0f, y + thick, zz, c);
        }
    }

    /* The lines along the floor, all meeting at the horizon. */
    for (int m = -12; m <= 12; m++) {
        const float x = (float)m * SW_LANES;
        float z_start = SW_Z_NEAR;
        const float reach = SW_FOCAL * (x < 0.0f ? -x : x) / 700.0f; /* where the line leaves the screen sideways */

        if (reach > z_start) {
            z_start = reach;
        }
        if (z_start >= SW_Z_FAR) {
            continue;
        }
        {
            const float xn = 320.0f + SW_FOCAL * x / z_start, yn = SW_HORIZON + SW_FOCAL * SW_CAM_H / z_start;
            const float xf = 320.0f + SW_FOCAL * x / SW_Z_FAR, yf = SW_HORIZON + SW_FOCAL * SW_CAM_H / SW_Z_FAR;
            const float wn = 1.2f, wf = 0.45f;
            const uint32_t cn = sw_pack(sw_mix(haze, grid, 1.0f - sqrtf((z_start - SW_Z_NEAR) / (SW_Z_FAR - SW_Z_NEAR))));
            const uint32_t cf = sw_pack(haze);

            sw_quad(xn - wn, yn, 1.0f / z_start, cn, xn + wn, yn, 1.0f / z_start, cn, xf - wf, yf, 1.0f / SW_Z_FAR, cf, xf + wf, yf,
                    1.0f / SW_Z_FAR, cf);
        }
    }

    /* The mountains, one mesh each side of the road. Rows run from near to far; the columns of a row are spread from the road's edge to
     * just past the edge of the screen. */
    for (int side = -1; side <= 1; side += 2) {
        static float sx[SW_MAX_ROWS + 1][SW_MAX_COLS + 1], sy[SW_MAX_ROWS + 1][SW_MAX_COLS + 1], sz[SW_MAX_ROWS + 1];
        static uint32_t col[SW_MAX_ROWS + 1][SW_MAX_COLS + 1], line_col[SW_MAX_ROWS + 1];

        for (int j = 0; j <= rows; j++) {
            const float u = (float)j / (float)rows;
            const float z = SW_Z_NEAR + powf(u, 1.5f) * (SW_Z_MOUNTAINS - SW_Z_NEAR);
            const float edge = 380.0f * z / SW_FOCAL;
            const float x_out = edge > SW_ROAD + 4.0f ? edge : SW_ROAD + 4.0f;
            const float fog = sw_smooth((z - 35.0f) / (SW_Z_FAR - 35.0f)) * 0.9f;

            sz[j] = 1.0f / z - 0.0002f;
            line_col[j] = sw_pack(sw_mix(sw_mix(mountain, grid, 0.9f), haze, fog));
            for (int i = 0; i <= cols; i++) {
                const float v = (float)i / (float)cols;
                const float ax = SW_ROAD + (x_out - SW_ROAD) * powf(v, 1.3f);
                const float h = sw_height(ax, z + sw_distance, amplitude);
                const float hn = h / amplitude;
                float y = SW_HORIZON + SW_FOCAL * (SW_CAM_H - h) / z;

                y = y < -200.0f ? -200.0f : y;
                sx[j][i] = 320.0f + (float)side * SW_FOCAL * ax / z;
                sy[j][i] = y;
                col[j][i] = sw_pack(sw_mix(sw_mix(sw_mix(ground, mountain, 0.5f + hn), grid, hn * hn * 0.45f), haze, fog));
            }
        }
        for (int j = 0; j < rows; j++) {
            for (int i = 0; i <= cols; i++) {
                sw_vertex(sx[j][i], sy[j][i], sz[j], col[j][i], 0);
                sw_vertex(sx[j + 1][i], sy[j + 1][i], sz[j + 1], col[j + 1][i], i == cols);
            }
        }
        /* The lines along the slopes (on the nearer rows only: far away they would only pile up on the horizon), each one a single strip so
         * that a 32 x 32 tile sees one entry for a line, not one for each of its pieces. */
        {
            int near_rows = 0;

            while (near_rows < rows && 1.0f / (sz[near_rows] + 0.0002f) <= 85.0f) {
                near_rows++;
            }
            for (int i = 0; i <= cols && near_rows > 0; i++) {
                for (int j = 0; j <= near_rows; j++) {
                    const float zl = sz[j] + 0.0003f;

                    sw_vertex(sx[j][i] - 0.6f, sy[j][i], zl, line_col[j], 0);
                    sw_vertex(sx[j][i] + 0.6f, sy[j][i], zl, line_col[j], j == near_rows);
                }
            }
        }
        /* A thin bright line along each row, where it is far enough from the one before to be told apart. */
        {
            float last_y = 1000.0f;

            for (int j = 0; j <= rows; j++) {
                const float row_y = sy[j][cols / 2];

                if ((j & 1) || (last_y - row_y < 4.0f && j > 0)) {
                    continue;
                }
                last_y = row_y;
                for (int i = 0; i <= cols; i++) {
                    const float zl = sz[j] + 0.0003f;

                    sw_vertex(sx[j][i], sy[j][i], zl, line_col[j], 0);
                    sw_vertex(sx[j][i], sy[j][i] + 1.5f, zl, line_col[j], i == cols);
                }
            }
        }
    }
}
