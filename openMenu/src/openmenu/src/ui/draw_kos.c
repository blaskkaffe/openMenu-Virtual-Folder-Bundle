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

/* An animated backdrop for themes that ask for one (THEME.INI backdrop=1): slow silk-like waves that glow in the theme's colour.
 * A grid of Gouraud-shaded strips in the opaque list. The shading comes from the slope of two travelling sine waves, so it needs no
 * texture and no video memory, and the grid is small (16x12 cells, about 400 vertices a frame). The theme's background picture is
 * then drawn over it with see-through areas. Call once per frame in the opaque pass. */
#define BACKDROP_COLS 16
#define BACKDROP_ROWS 12

void
draw_backdrop(uint32_t accent) {
    static uint32_t frame = 0;
    static float xs[BACKDROP_ROWS + 1][BACKDROP_COLS + 1];
    static float ys[BACKDROP_ROWS + 1][BACKDROP_COLS + 1];
    static uint32_t cs[BACKDROP_ROWS + 1][BACKDROP_COLS + 1];
    const float t = (float)frame * (1.0f / 60.0f);
    const float two_pi = 6.2831853f;
    const float base = 16.0f;
    const float glow_r = (float)((accent >> 16) & 0xFF) * 0.55f - base;
    const float glow_g = (float)((accent >> 8) & 0xFF) * 0.55f - base;
    const float glow_b = (float)(accent & 0xFF) * 0.55f - base;

    frame++;

    for (int j = 0; j <= BACKDROP_ROWS; j++) {
        for (int i = 0; i <= BACKDROP_COLS; i++) {
            /* The grid reaches past the screen so the waves never show an edge. */
            const float x = -40.0f + (float)i * (720.0f / BACKDROP_COLS);
            const float y = -40.0f + (float)j * (560.0f / BACKDROP_ROWS);
            const float xn = x / 640.0f;
            const float yn = y / 480.0f;
            const float a = two_pi * (1.1f * xn + 0.4f * yn) + t * 0.9f;
            const float b = two_pi * (0.5f * xn - 0.9f * yn) - t * 0.7f;
            const float ca = fcos(a);
            const float cb = fcos(b);
            const float h = 0.6f * fsin(a) + 0.4f * fsin(b);
            /* Slope of the surface, lit from the upper left. */
            const float dx = 0.66f * ca + 0.2f * cb;
            const float dy = 0.24f * ca - 0.36f * cb;
            float light = 0.5f + 0.5f * (dx - dy) * 0.9f;

            if (light < 0.0f) {
                light = 0.0f;
            } else if (light > 1.0f) {
                light = 1.0f;
            }
            light *= light;
            xs[j][i] = x;
            ys[j][i] = y + h * 9.0f;
            cs[j][i] = 0xFF000000u | ((uint32_t)(base + glow_r * light) << 16) | ((uint32_t)(base + glow_g * light) << 8)
                       | (uint32_t)(base + glow_b * light);
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

/* Popup corners. A theme can ask for rounded popups (THEME.INI menu_corner_radius). Zero keeps the square frame. */
static int popup_corner_radius = 0;

void
draw_set_corner_radius(int radius) {
    popup_corner_radius = radius < 0 ? 0 : (radius > 16 ? 16 : radius);
}

/* A filled rectangle with rounded top and/or bottom corners, drawn as one-pixel-high strips at each rounded end and a
 * plain quad between them. */
static void
draw_rounded_quad(int x, int y, int width, int height, int radius, int round_top, int round_bottom, uint32_t color) {
    if (radius > height / 2) {
        radius = height / 2;
    }
    if (radius > width / 2) {
        radius = width / 2;
    }
    if (radius <= 0) {
        draw_draw_quad(x, y, (float)width, (float)height, color);
        return;
    }
    const int top = round_top ? radius : 0;
    const int bottom = round_bottom ? radius : 0;

    if (height - top - bottom > 0) {
        draw_draw_quad(x, y + top, (float)width, (float)(height - top - bottom), color);
    }
    for (int i = 0; i < radius; i++) {
        /* Row i from the outer edge: how far the circle of the corner is from the side. */
        const float dy = (float)radius - (float)i - 0.5f;
        const int inset = radius - (int)(sqrtf((float)(radius * radius) - dy * dy) + 0.5f);

        if (round_top) {
            draw_draw_quad(x + inset, y + i, (float)(width - 2 * inset), 1.0f, color);
        }
        if (round_bottom) {
            draw_draw_quad(x + inset, y + height - 1 - i, (float)(width - 2 * inset), 1.0f, color);
        }
    }
}

/* A popup's frame: a 2 px border, the fill, and with header_height above zero a header bar in the border colour. */
void
draw_draw_popup_frame(int x, int y, int width, int height, int header_height, uint32_t border_color, uint32_t fill_color) {
    const int r = popup_corner_radius;

    if (r == 0) {
        draw_draw_quad(x - 2, y - 2, (float)(width + 4), (float)(height + 4), border_color);
        draw_draw_quad(x, y, (float)width, (float)height, fill_color);
        if (header_height > 0) {
            draw_draw_quad(x, y, (float)width, (float)header_height, border_color);
        }
        return;
    }
    draw_rounded_quad(x - 2, y - 2, width + 4, height + 4, r + 2, 1, 1, border_color);
    draw_rounded_quad(x, y, width, height, r, 1, 1, fill_color);
    if (header_height > 0) {
        draw_rounded_quad(x, y, width, header_height, r, 1, 0, border_color);
    }
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
