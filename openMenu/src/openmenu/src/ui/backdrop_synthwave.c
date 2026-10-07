/*
 * File: backdrop_synthwave.c
 * Project: ui
 * Synthwave 3D backdrop: a neon grid floor coming towards the screen, mountains that rise and fall a little as they pass
 * and a striped sun on the horizon (THEME.INI backdrop=synthwave)
 */

#include <math.h>
#include <stdint.h>

#include "ui/backdrop.h"
#include "ui/draw_prototypes.h"

#define HORIZON 236.0f /* screen row of the horizon */
#define FOCAL   420.0f
#define CAM_H   5.0f
#define Z_NEAR  (FOCAL * CAM_H / (480.0f - HORIZON)) /* depth at the bottom of the screen */
#define Z_FAR   140.0f                               /* the lines along the floor end here */
#define Z_LINES 96.0f                                /* the lines across it end here */
#define Z_PEAKS 105.0f                               /* the farthest row of the mountains */
#define STEP    8.0f                                 /* distance between the lines across the floor */
#define LANE    3.2f                                 /* distance between the lines along it */
#define ROAD    9.0f                                 /* the flat floor, either side of the middle */
#define ROWS    12
#define COLS    7

static uint32_t frame = 0;
static float travelled = 0.0f;

/* Mixes two 0xRRGGBB colors into an opaque argb */
static uint32_t
mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0xFF000000u;

    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    for (int shift = 16; shift >= 0; shift -= 8) {
        const int ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;

        out |= (uint32_t)(ca + (int)((float)(cb - ca) * t)) << shift;
    }
    return out;
}

static void
vertex(float x, float y, float z, uint32_t argb, int last) {
    pvr_vertex_t vert = {.argb = argb, .oargb = 0, .flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX, .x = x, .y = y, .z = z, .u = 0, .v = 0};

    pvr_prim(&vert, sizeof(vert));
}

/* Draws a horizontal bar with a color at its top and one at its bottom */
static void
bar(float x0, float x1, float y0, float y1, float z, uint32_t top, uint32_t bottom) {
    vertex(x0, y0, z, top, 0);
    vertex(x1, y0, z, top, 0);
    vertex(x0, y1, z, bottom, 0);
    vertex(x1, y1, z, bottom, 1);
}

/* Height of the mountains at a position: ax is the distance from the middle, zw the distance along the world.
 * The frame count makes them breathe */
static float
height(float ax, float zw) {
    const float ramp = (ax - ROAD - 1.0f) * 0.125f;
    const float n = 0.5f * fsin(0.31f * zw + 0.25f * ax) + 0.3f * fsin(0.77f * zw - 0.43f * ax + 1.3f) + 0.2f * fsin(1.41f * zw + 0.7f * ax + 2.1f);
    const float peak = 0.5f + 0.5f * n;
    const float breath = 1.0f + 0.16f * fsin(0.045f * (float)frame + 0.11f * zw);

    return 13.0f * (ramp < 0.0f ? 0.0f : (ramp > 1.0f ? 1.0f : ramp)) * (0.08f + 0.92f * peak * peak * peak) * breath;
}

/* colors: sky, horizon, sun and grid, as 0xRRGGBB */
void
backdrop_synthwave_draw(const uint32_t* colors) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const uint32_t sky = colors[0], horizon = colors[1], sun = colors[2], grid = colors[3];
    const uint32_t mountain = mix(sky, grid, 0.15f);
    const uint32_t haze = mix(horizon, sky, 0.45f);
    const float offset = fmodf(travelled, STEP);

    frame++;
    travelled += 0.35f;

    pvr_poly_cxt_col(&context, draw_get_list());
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    bar(0.0f, 640.0f, 0.0f, HORIZON + 2.0f, 0.0001f, sky, horizon);

    /* Sun, cut into stripes that get wider towards the horizon. The floor covers its lower half */
    for (float y = HORIZON - 126.0f; y < HORIZON;) {
        const float f = (y - (HORIZON - 126.0f)) / 126.0f;
        const float step = f < 0.35f ? 3.0f : 7.0f;
        const float gap = f < 0.35f ? 0.0f : (f - 0.35f) * 7.7f;
        const float dy = y + (step - gap) * 0.5f - (HORIZON - 34.0f);
        const float chord = 92.0f * 92.0f - dy * dy;

        if (chord > 0.0f && step - gap > 0.5f) {
            bar(320.0f - sqrtf(chord), 320.0f + sqrtf(chord), y, y + step - gap, 0.0003f, mix(sun, horizon, f), mix(sun, horizon, f + step / 126.0f));
        }
        y += step;
    }

    bar(0.0f, 640.0f, HORIZON, 480.0f, 0.001f, mix(sky, haze, 0.55f), 0xFF000000u | sky);

    /* Lines across the floor */
    for (float z = ceilf((Z_NEAR + offset) / STEP) * STEP - offset; z <= Z_LINES; z += STEP) {
        const float y = HORIZON + FOCAL * CAM_H / z;
        const float thick = FOCAL * 0.07f / z < 1.0f ? 1.0f : FOCAL * 0.07f / z;
        const uint32_t color = mix(haze, grid, 1.0f - sqrtf((z - Z_NEAR) / (Z_FAR - Z_NEAR)));

        bar(0.0f, 640.0f, y, y + thick, 1.0f / z, color, color);
    }

    /* Lines along the floor, meeting at the horizon */
    for (int m = -12; m <= 12; m++) {
        const float x = (float)m * LANE;
        const float edge = FOCAL * fabsf(x) / 700.0f; /* where the line leaves the screen sideways */
        const float z = edge > Z_NEAR ? edge : Z_NEAR;

        if (z < Z_FAR) {
            const uint32_t color = mix(haze, grid, 1.0f - sqrtf((z - Z_NEAR) / (Z_FAR - Z_NEAR)));
            const float xn = 320.0f + FOCAL * x / z, yn = HORIZON + FOCAL * CAM_H / z;
            const float xf = 320.0f + FOCAL * x / Z_FAR, yf = HORIZON + FOCAL * CAM_H / Z_FAR;

            vertex(xn - 1.2f, yn, 1.0f / z, color, 0);
            vertex(xn + 1.2f, yn, 1.0f / z, color, 0);
            vertex(xf - 0.45f, yf, 1.0f / Z_FAR, haze, 0);
            vertex(xf + 0.45f, yf, 1.0f / Z_FAR, haze, 1);
        }
    }

    /* Mountains on both sides of the road. A row spreads from the edge of the road to just past the edge of the screen */
    for (int side = -1; side <= 1; side += 2) {
        float sx[ROWS + 1][COLS + 1], sy[ROWS + 1][COLS + 1], sz[ROWS + 1];
        uint32_t color[ROWS + 1][COLS + 1], line[ROWS + 1];
        int near_rows = 0;

        for (int j = 0; j <= ROWS; j++) {
            const float z = Z_NEAR + powf((float)j / ROWS, 1.5f) * (Z_PEAKS - Z_NEAR);
            const float edge = 380.0f * z / FOCAL;
            const float width = (edge > ROAD + 4.0f ? edge : ROAD + 4.0f) - ROAD;
            const float fog = (z - 35.0f) / (Z_PEAKS - 35.0f) * 0.9f;

            sz[j] = 1.0f / z - 0.0002f; /* a little behind the lines of the floor */
            line[j] = mix(mix(mountain, grid, 0.9f), haze, fog);
            near_rows += z < 85.0f;
            for (int i = 0; i <= COLS; i++) {
                const float ax = ROAD + width * powf((float)i / COLS, 1.3f);
                const float h = height(ax, z + travelled);

                sx[j][i] = 320.0f + (float)side * FOCAL * ax / z;
                sy[j][i] = HORIZON + FOCAL * (CAM_H - h) / z;
                color[j][i] = mix(mix(mix(sky, mountain, 0.5f + h / 13.0f), grid, h * h / 169.0f * 0.45f), haze, fog);
            }
        }
        for (int j = 0; j < ROWS; j++) {
            for (int i = 0; i <= COLS; i++) {
                vertex(sx[j][i], sy[j][i], sz[j], color[j][i], 0);
                vertex(sx[j + 1][i], sy[j + 1][i], sz[j + 1], color[j + 1][i], i == COLS);
            }
        }

        /* Lines along the slopes and across them (every other row), on the near rows only, each one a single strip */
        for (int i = 0; i <= COLS; i++) {
            for (int j = 0; j < near_rows; j++) {
                vertex(sx[j][i] - 0.6f, sy[j][i], sz[j] + 0.0003f, line[j], 0);
                vertex(sx[j][i] + 0.6f, sy[j][i], sz[j] + 0.0003f, line[j], j == near_rows - 1);
            }
        }
        for (int j = 0; j < near_rows; j += 2) {
            for (int i = 0; i <= COLS; i++) {
                vertex(sx[j][i], sy[j][i], sz[j] + 0.0003f, line[j], 0);
                vertex(sx[j][i], sy[j][i] + 1.5f, sz[j] + 0.0003f, line[j], i == COLS);
            }
        }
    }
}
