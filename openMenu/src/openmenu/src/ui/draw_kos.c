/*
 * File: draw_kos.c
 * Project: ui
 * File Created: Wednesday, 19th May 2021 9:33:03 pm
 * Author: Hayden Kowalchuk
 * -----
 * Copyright (c) 2021 Hayden Kowalchuk, Hayden Kowalchuk
 * License: BSD 3-clause "New" or "Revised" License,
 * http://www.opensource.org/licenses/BSD-3-Clause
 */

#include <math.h>
#include <stdio.h>

#include <backend/dat_format.h>
#include "ui/draw_prototypes.h"
#include "ui/font_prototypes.h"

#include "ui/draw_kos.h"

image img_empty_boxart;
image img_dir_boxart;

static int current_list;

void
draw_set_list(int list) {
    current_list = list;
}

int
draw_get_list(void) {
    return current_list;
}

static float z_depth;

float
z_get(void) {
    return z_depth;
}

float
z_set(float z) {
    z_depth = z;
    return z_depth;
}

float
z_set_cond(float z) {
    if (z > z_depth) {
        z_depth = z;
    } else {
        z_inc();
    }
    return z_depth;
}

void
z_reset(void) {
    z_depth = 1.0f;
}

float
z_inc(void) {
    z_depth += 1.0f;

    /* 512 Puts Blit infront of everything*/
    if (z_depth > 512.0f) {
        z_depth = 512.0f;
    }
    return z_depth;
}

static void* pvr_scratch_buf;

/* Called only once at start */
void
draw_init(void) {
    pvr_scratch_buf = pvr_mem_malloc(TEXMAN_BUFFER_SIZE);
    texman_reset(pvr_scratch_buf, TEXMAN_BUFFER_SIZE);

    z_reset();
}

/* called at the start of each frame */
void
draw_setup(void) {
    texman_reset(pvr_scratch_buf, TEXMAN_BUFFER_SIZE);
}

void*
draw_load_missing_icon(void* user) {
    image* img = (image*)user;
    img->texture = img_empty_boxart.texture;
    img->width = img_empty_boxart.width;
    img->height = img_empty_boxart.height;
    img->format = img_empty_boxart.format;
    return img;
}

/* Throws ID into id and returns something if needs to*/
void*
draw_load_texture(const char* filename, void* user) {
    image* img = (image*)user;

    pvr_ptr_t txr;

    if (!(txr = load_pvr(filename, &img->width, &img->height, &img->format))) {
        img->texture = img_empty_boxart.texture;
        img->width = img_empty_boxart.width;
        img->height = img_empty_boxart.height;
        img->format = img_empty_boxart.format;
        return img;
    }
    img->texture = txr;

    return user;
}

void*
draw_load_texture_buffer(const char* filename, void* user, void* buffer) {
    image* img = (image*)user;

    pvr_ptr_t txr;

    if (!(txr = load_pvr_to_buffer(filename, &img->width, &img->height, &img->format, buffer))) {
        img->texture = img_empty_boxart.texture;
        img->width = img_empty_boxart.width;
        img->height = img_empty_boxart.height;
        img->format = img_empty_boxart.format;
        return img;
    }
    img->texture = txr;

    return user;
}

void*
draw_load_texture_from_DAT_to_buffer(const struct dat_file* bin, const char* ID, void* user, void* buffer) {
    image* img = (image*)user;
    pvr_ptr_t txr;
    int ret = DAT_read_file_by_ID(bin, ID, pvr_get_internal_buffer());
    /* printf("DAT: read ID='%s' ret=%d\n", ID, ret); */
    if (!ret) {
        img->texture = img_empty_boxart.texture;
        img->width = img_empty_boxart.width;
        img->height = img_empty_boxart.height;
        img->format = img_empty_boxart.format;
        return img;
    }

    txr = load_pvr_from_buffer_to_buffer(pvr_get_internal_buffer(), &img->width, &img->height, &img->format, buffer);
    img->texture = txr;
    /* printf("DAT: img w=%lu h=%lu fmt=%lu\n", img->width, img->height, img->format); */

    return user;
}

/* draws an image at coords of a given size */
void
draw_draw_image(int x, int y, float width, float height, uint32_t color, void* user) {
    image* img = (image*)user;
    const dimen_RECT uv_01 = {.x = 0, .y = 0, .w = img->width, .h = img->height};
    draw_draw_sub_image(x, y, width, height, color, user, &uv_01);
}

void
draw_draw_sub_image(int x, int y, float width, float height, uint32_t color, void* user, const dimen_RECT* rect) {
    image* img = (image*)user;

    if (img == NULL || img->width == 0 || img->height == 0) {
        return;
    }

    /* Upper left */
    const float x1 = round((float)x);
    const float y1 = round((float)y);
    const float u1 = (float)rect->x / img->width;
    const float v1 = (float)rect->y / img->height;

    /* Lower right */
    const float x2 = round((float)x + width);
    const float y2 = round((float)y + height);
    const float u2 = (float)(rect->x + rect->w) / img->width;
    const float v2 = (float)(rect->y + rect->h) / img->height;

    const float z = z_inc();

#ifdef KOS_SPRITE
    pvr_sprite_cxt_t context;
    pvr_sprite_hdr_t header;

    pvr_sprite_cxt_txr(&context, draw_get_list(), img->format, img->width, img->height, img->texture,
                       PVR_FILTER_BILINEAR);
    pvr_sprite_compile(&header, &context);

    pvr_prim(&header, sizeof(header));

    pvr_sprite_txr_t vert = {
        .flags = PVR_CMD_VERTEX_EOL, /* Always? */
        /*  upper left */
        .ax = x1,
        .ay = y1,
        .az = z,
        /* upper right */
        .bx = x2,
        .by = y1,
        .bz = z,
        /* lower left */
        .cx = x2,
        .cy = y2,
        .cz = z,
        /* interpolated */
        .dx = x1,
        .dy = y2,
        .auv = PVR_PACK_16BIT_UV(u1, v1), /* UVS */
        .buv = PVR_PACK_16BIT_UV(u2, v1), /* UVS */
        .cuv = PVR_PACK_16BIT_UV(u2, v2), /* UVS */
    };
    pvr_prim(&vert, sizeof(vert));

#else
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    pvr_poly_cxt_txr(&context, draw_get_list(), img->format, img->width, img->height, img->texture,
                     PVR_FILTER_BILINEAR);
    if (context.txr.enable != PVR_TEXTURE_DISABLE) {
        switch (context.txr.width) {
            case 8:
            case 16:
            case 32:
            case 64:
            case 128:
            case 256:
            case 512:
            case 1024: break;
            default:
                /* printf("%s error tex size %d(%ld) %d(%ld)\n", __func__, context.txr.width, img->width,
                   context.txr.height, img->height); */
                return;
                break;
        }
    }
    pvr_poly_compile(&header, &context);

    pvr_prim(&header, sizeof(header));

    pvr_vertex_t vert = {.argb = color, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = 1};

    vert.x = x1;
    vert.y = y2;
    vert.z = z;
    vert.u = u1;
    vert.v = v2;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x1;
    vert.y = y1;
    vert.u = u1;
    vert.v = v1;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x2;
    vert.y = y2;
    vert.u = u2;
    vert.v = v2;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x2;
    vert.y = y1;
    vert.u = u2;
    vert.v = v1;
    pvr_prim(&vert, sizeof(vert));
#endif
}

/* Draws untextured quad at coords with size and color(rgba) */
void
draw_draw_quad(int x, int y, float width, float height, uint32_t color) {
    /* Upper left */
    const float x1 = round((float)x);
    const float y1 = round((float)y);

    /* Lower right */
    const float x2 = round((float)x + width);
    const float y2 = round((float)y + height);

    const float z = z_inc();

#ifdef KOS_SPRITE
    pvr_sprite_cxt_t context;
    pvr_sprite_hdr_t header;

    pvr_sprite_cxt_col(&context, draw_get_list());
    pvr_sprite_compile(&header, &context);

    header.argb = color;

    pvr_prim(&header, sizeof(header));

    pvr_sprite_col_t vert = {.flags = PVR_CMD_VERTEX_EOL, /* Always? */
                             /*  upper left */
                             .ax = x1,
                             .ay = y1,
                             .az = z,
                             /* upper right */
                             .bx = x2,
                             .by = y1,
                             .bz = z,
                             /* lower left */
                             .cx = x2,
                             .cy = y2,
                             .cz = z,
                             /* interpolated */
                             .dx = x1,
                             .dy = y2};

    pvr_prim(&vert, sizeof(vert));

#else
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    pvr_poly_cxt_col(&context, draw_get_list());
    if (context.txr.enable != PVR_TEXTURE_DISABLE) {
        switch (context.txr.width) {
            case 8:
            case 16:
            case 32:
            case 64:
            case 128:
            case 256:
            case 512:
            case 1024: break;
            default:
                /* printf("%s error tex size %d(%f) %d(%f)\n", __func__, context.txr.width, width, context.txr.height,
                   height); */
                return;
                break;
        }
    }
    pvr_poly_compile(&header, &context);

    pvr_prim(&header, sizeof(header));

    pvr_vertex_t vert = {.argb = color, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = z, .u = 0, .v = 0};

    vert.x = x1;
    vert.y = y2;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x1;
    vert.y = y1;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x2;
    vert.y = y2;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x2;
    vert.y = y1;
    pvr_prim(&vert, sizeof(vert));
#endif
}

/* A small telephone handset (10 x 5 px), drawn from quads: the mark for a game that somebody is playing online. */
void
draw_draw_phone_icon(int x, int y, uint32_t color) {
    /* Row by row: where the filled runs start and end (end exclusive). Two runs per row at most. */
    static const signed char runs[5][4] = {{2, 8, -1, -1}, {1, 9, -1, -1}, {0, 3, 7, 10}, {0, 3, 7, 10}, {0, 2, 8, 10}};

    for (int row = 0; row < 5; row++) {
        for (int r = 0; r < 4; r += 2) {
            if (runs[row][r] >= 0) {
                draw_draw_quad(x + runs[row][r], y + row, (float)(runs[row][r + 1] - runs[row][r]), 1.0f, color);
            }
        }
    }
}

/* An animated backdrop for themes that ask for one (THEME.INI backdrop=1): a rolling, glowing surface in the theme's colour that
 * fills the screen behind the menu. A grid of Gouraud-shaded strips in the opaque list, with no texture and no video memory.
 *
 * Three travelling sine waves make a height field. Each grid point is lit by its slope (a soft diffuse light plus a narrow shine along the
 * ridges, which is what makes it read as a moving 3D surface) and is pushed away from or toward the screen centre by its height, the
 * way a camera over the surface would see it, so the grid really swells and sinks. 24 x 18 cells, about 900 vertices a frame. */
#define BACKDROP_COLS 24
#define BACKDROP_ROWS 18

void
draw_backdrop(uint32_t accent) {
    static uint32_t frame = 0;
    static float xs[BACKDROP_ROWS + 1][BACKDROP_COLS + 1];
    static float ys[BACKDROP_ROWS + 1][BACKDROP_COLS + 1];
    static uint32_t cs[BACKDROP_ROWS + 1][BACKDROP_COLS + 1];
    const float t = (float)frame * (1.0f / 60.0f);
    const float ar = (float)((accent >> 16) & 0xFF);
    const float ag = (float)((accent >> 8) & 0xFF);
    const float ab = (float)(accent & 0xFF);

    frame++;

    for (int j = 0; j <= BACKDROP_ROWS; j++) {
        for (int i = 0; i <= BACKDROP_COLS; i++) {
            /* The grid reaches past the screen so the swell never shows an edge. */
            const float gx = -60.0f + (float)i * (760.0f / BACKDROP_COLS);
            const float gy = -60.0f + (float)j * (600.0f / BACKDROP_ROWS);
            const float u = gx * (1.0f / 100.0f);
            const float v = gy * (1.0f / 100.0f);
            /* Long waves (300 to 500 px): the grid has a point every 32 px, so anything shorter would alias into streaks. */
            const float a1 = 1.25f * u + 1.05f * v + 3.0f * t;
            const float a2 = 0.85f * u - 1.15f * v - 2.3f * t;
            const float a3 = 1.9f * u + 0.45f * v + 1.7f * t;
            const float c1 = fcos(a1);
            const float c2 = fcos(a2);
            const float c3 = fcos(a3);
            const float h = 1.7f * fsin(a1) + 1.1f * fsin(a2) + 0.6f * fsin(a3);
            const float hx = 1.7f * 1.25f * c1 + 1.1f * 0.85f * c2 + 0.6f * 1.9f * c3;
            const float hy = 1.7f * 1.05f * c1 - 1.1f * 1.15f * c2 + 0.6f * 0.45f * c3;
            /* Surface normal (-hx, -hy, 1) scaled to slopes of about one, against a light from the upper left in front, and the
             * same normal against the half way vector for the narrow shine along the ridges. */
            const float sx = hx * 0.2f;
            const float sy = hy * 0.2f;
            const float inv = 1.0f / sqrtf(sx * sx + sy * sy + 1.0f);
            float diffuse = (0.45f * sx + 0.45f * sy + 0.77f) * inv;
            float shine = (0.25f * sx + 0.25f * sy + 0.93f) * inv;
            /* Dim toward the screen edges, a little like a vignette. */
            const float ex = (gx - 320.0f) * (1.0f / 460.0f);
            const float ey = (gy - 240.0f) * (1.0f / 360.0f);
            float edge = 1.0f - 0.45f * (ex * ex + ey * ey);

            if (diffuse < 0.0f) {
                diffuse = 0.0f;
            } else if (diffuse > 1.0f) {
                diffuse = 1.0f;
            }
            if (shine < 0.0f) {
                shine = 0.0f;
            } else if (shine > 1.0f) {
                shine = 1.0f;
            }
            if (edge < 0.2f) {
                edge = 0.2f;
            }
            shine = shine * shine;
            shine = shine * shine; /* to the 4th power: a sharper shine than this aliases against the coarse grid */
            xs[j][i] = 320.0f + (gx - 320.0f) * (1.0f + 0.030f * h);
            ys[j][i] = 240.0f + (gy - 240.0f) * (1.0f + 0.030f * h);

            {
                /* A dark floor, the theme colour in the lit parts, and a pale shine on the ridges. */
                const float body = (0.12f + 0.88f * diffuse * diffuse) * edge;
                const float spark = shine * 0.35f * edge;
                float r = 10.0f + ar * 0.85f * body + (255.0f - ar) * spark * 0.5f;
                float g = 10.0f + ag * 0.85f * body + (255.0f - ag) * spark * 0.5f;
                float b = 10.0f + ab * 0.85f * body + (255.0f - ab) * spark * 0.5f;

                r = r > 255.0f ? 255.0f : r;
                g = g > 255.0f ? 255.0f : g;
                b = b > 255.0f ? 255.0f : b;
                cs[j][i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }

    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const float z = z_inc();

    pvr_poly_cxt_col(&context, draw_get_list());
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    pvr_vertex_t vert = {.argb = 0, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = z, .u = 0, .v = 0};

    for (int j = 0; j < BACKDROP_ROWS; j++) {
        for (int i = 0; i <= BACKDROP_COLS; i++) {
            /* The same order the plain quads use: the lower point, then the upper one. */
            vert.flags = PVR_CMD_VERTEX;
            vert.x = xs[j + 1][i];
            vert.y = ys[j + 1][i];
            vert.argb = cs[j + 1][i];
            pvr_prim(&vert, sizeof(vert));

            vert.flags = i == BACKDROP_COLS ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
            vert.x = xs[j][i];
            vert.y = ys[j][i];
            vert.argb = cs[j][i];
            pvr_prim(&vert, sizeof(vert));
        }
    }
}

/* Rounded rectangles as real polygons. A theme can ask for rounded popups (THEME.INI menu_corner_radius; zero keeps the square frame)
 * and, with a backdrop, for glass panels (draw_draw_panel). Each corner is an arc of ARC_SEG segments; a convex outline is drawn as one
 * triangle strip by zig-zagging across it (v0, v1, vn-1, v2, vn-2 ...). */
#define ARC_SEG    6
#define OUTLINE_MAX (4 * (ARC_SEG + 1))

static int popup_corner_radius = 0;

void
draw_set_corner_radius(int radius) {
    popup_corner_radius = radius < 0 ? 0 : (radius > 16 ? 16 : radius);
}

/* The outline of a rectangle with its own radius at each corner (top left, top right, bottom right, bottom left), clockwise. A corner
 * with radius 0 is one point. Returns the number of points. */
static int
rr_outline(float x, float y, float w, float h, float r0, float r1, float r2, float r3, float* px, float* py) {
    const float radii[4] = {r0, r1, r2, r3};
    const float cx[4] = {x + r0, x + w - r1, x + w - r2, x + r3};
    const float cy[4] = {y + r0, y + r1, y + h - r2, y + h - r3};
    const float corner_x[4] = {x, x + w, x + w, x};
    const float corner_y[4] = {y, y, y + h, y + h};
    int n = 0;

    for (int c = 0; c < 4; c++) {
        if (radii[c] <= 0.5f) {
            px[n] = corner_x[c];
            py[n++] = corner_y[c];
            continue;
        }
        for (int k = 0; k <= ARC_SEG; k++) {
            const float a = (float)(2 + c) * 1.5707963f + (float)k * (1.5707963f / ARC_SEG);

            px[n] = cx[c] + radii[c] * fcos(a);
            py[n++] = cy[c] + radii[c] * fsin(a);
        }
    }
    return n;
}

/* A solid or vertically graded convex polygon (the top colour at the smallest y, the bottom colour at the largest). */
static void
draw_convex(const float* px, const float* py, int n, float top, float bottom, uint32_t color_top, uint32_t color_bottom) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const float z = z_inc();
    const float span = bottom - top > 1.0f ? bottom - top : 1.0f;
    const int ta = (color_top >> 24) & 0xFF, tr = (color_top >> 16) & 0xFF, tg = (color_top >> 8) & 0xFF, tb = color_top & 0xFF;
    const int ba = (color_bottom >> 24) & 0xFF, br = (color_bottom >> 16) & 0xFF, bg = (color_bottom >> 8) & 0xFF,
              bb = color_bottom & 0xFF;

    pvr_poly_cxt_col(&context, draw_get_list());
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    pvr_vertex_t vert = {.argb = 0, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = z, .u = 0, .v = 0};

    for (int k = 0; k < n; k++) {
        const int idx = (k & 1) ? n - 1 - (k >> 1) : (k >> 1) + 0;
        const float f = (py[idx] - top) / span;
        const int a = ta + (int)((float)(ba - ta) * f);
        const int r = tr + (int)((float)(br - tr) * f);
        const int g = tg + (int)((float)(bg - tg) * f);
        const int b = tb + (int)((float)(bb - tb) * f);

        vert.flags = k == n - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vert.x = px[idx];
        vert.y = py[idx];
        vert.argb = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        pvr_prim(&vert, sizeof(vert));
    }
}

/* The band between an outline and the same outline inset by bw, as one strip. */
static void
draw_ring(float x, float y, float w, float h, float r, float bw, uint32_t color) {
    float ox[OUTLINE_MAX], oy[OUTLINE_MAX], ix[OUTLINE_MAX], iy[OUTLINE_MAX];
    const float ri = r > bw ? r - bw : 0.0f;
    const int n = rr_outline(x, y, w, h, r, r, r, r, ox, oy);
    const int m = rr_outline(x + bw, y + bw, w - 2.0f * bw, h - 2.0f * bw, ri, ri, ri, ri, ix, iy);
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const float z = z_inc();

    if (n != m) {
        return;
    }
    pvr_poly_cxt_col(&context, draw_get_list());
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    pvr_vertex_t vert = {.argb = color, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = z, .u = 0, .v = 0};

    for (int k = 0; k <= n; k++) {
        const int idx = k % n;

        vert.flags = PVR_CMD_VERTEX;
        vert.x = ox[idx];
        vert.y = oy[idx];
        pvr_prim(&vert, sizeof(vert));
        vert.flags = k == n ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vert.x = ix[idx];
        vert.y = iy[idx];
        pvr_prim(&vert, sizeof(vert));
    }
}

/* A popup's frame: a 2 px border, the fill, and with header_height above zero a header bar in the border colour. */
void
draw_draw_popup_frame(int x, int y, int width, int height, int header_height, uint32_t border_color, uint32_t fill_color) {
    const float r = (float)popup_corner_radius;

    if (popup_corner_radius == 0) {
        draw_draw_quad(x - 2, y - 2, (float)(width + 4), (float)(height + 4), border_color);
        draw_draw_quad(x, y, (float)width, (float)height, fill_color);
        if (header_height > 0) {
            draw_draw_quad(x, y, (float)width, (float)header_height, border_color);
        }
        return;
    }
    {
        float px[OUTLINE_MAX], py[OUTLINE_MAX];
        int n = rr_outline((float)(x - 2), (float)(y - 2), (float)(width + 4), (float)(height + 4), r + 2.0f, r + 2.0f, r + 2.0f,
                           r + 2.0f, px, py);

        draw_convex(px, py, n, (float)(y - 2), (float)(y + height + 2), border_color, border_color);
        n = rr_outline((float)x, (float)y, (float)width, (float)height, r, r, r, r, px, py);
        draw_convex(px, py, n, (float)y, (float)(y + height), fill_color, fill_color);
        if (header_height > 0) {
            n = rr_outline((float)x, (float)y, (float)width, (float)header_height, r, r, 0.0f, 0.0f, px, py);
            draw_convex(px, py, n, (float)y, (float)(y + header_height), border_color, border_color);
        }
    }
}

/* A glass panel for backdrop themes: a soft shadow, a translucent fill that is a little lighter at the top, and a border. Colours are
 * 0xRRGGBB; alpha (0..255) is the fill's opacity at the top, the bottom is a third more transparent. */
void
draw_draw_panel(int x, int y, int width, int height, int radius, int border_width, uint32_t border_rgb, uint32_t fill_rgb, int alpha) {
    float px[OUTLINE_MAX], py[OUTLINE_MAX];
    const float r = (float)radius;
    int n = rr_outline((float)(x + 3), (float)(y + 5), (float)width, (float)height, r, r, r, r, px, py);
    const uint32_t rgb = fill_rgb & 0x00FFFFFFu;
    const uint32_t light = (((rgb >> 16) & 0xFF) * 3 / 2 > 255 ? 255 : ((rgb >> 16) & 0xFF) * 3 / 2) << 16
                           | (((rgb >> 8) & 0xFF) * 3 / 2 > 255 ? 255 : ((rgb >> 8) & 0xFF) * 3 / 2) << 8
                           | (((rgb & 0xFF) * 3 / 2 > 255 ? 255 : (rgb & 0xFF) * 3 / 2));

    draw_convex(px, py, n, (float)(y + 5), (float)(y + 5 + height), 0x50000000u, 0x30000000u);                 /* the shadow */
    n = rr_outline((float)x, (float)y, (float)width, (float)height, r, r, r, r, px, py);
    draw_convex(px, py, n, (float)y, (float)(y + height), ((uint32_t)alpha << 24) | light, ((uint32_t)(alpha * 2 / 3) << 24) | rgb);
    draw_ring((float)x, (float)y, (float)width, (float)height, r, (float)border_width, 0xFF000000u | border_rgb);
}

/* draws an image at coords as a square */
void
draw_draw_square(int x, int y, float size, uint32_t color, void* user) {
    draw_draw_image(x, y, size, size, color, user);
}

void
draw_draw_image_centered(int x, int y, float width, float height, uint32_t color, void* user) {
    const int x_extent = width / 2;
    const int y_extent = height / 2;
    draw_draw_image(x - x_extent, y - y_extent, width, height, color, user);
}
