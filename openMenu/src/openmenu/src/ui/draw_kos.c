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

/* The animated 3D backdrops of Folders themes are in backdrop.c, backdrop_waves.c and backdrop_synthwave.c. */

/* Rounded rectangles for popups (THEME.INI menu_corner_radius; zero keeps the square frame) and, with a backdrop, glass panels
 * (draw_draw_panel). They are built from plain axis-aligned quads, one per run of pixel rows that share the same left and right end,
 * so a corner is a small staircase. (Triangle strips along an arc showed notches and holes on the console.) All quads of one shape
 * share one depth value: they never overlap. */
static int popup_corner_radius = 0;

void
draw_set_corner_radius(int radius) {
    popup_corner_radius = radius < 0 ? 0 : (radius > 16 ? 16 : radius);
}

typedef struct {
    float top, bottom;
    uint32_t color_top, color_bottom;
} rr_fill_t;

static uint32_t
rr_color_at(const rr_fill_t* g, float y) {
    const float span = g->bottom - g->top > 1.0f ? g->bottom - g->top : 1.0f;
    float f = (y - g->top) / span;
    uint32_t out = 0;

    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    for (int shift = 24; shift >= 0; shift -= 8) {
        const int c0 = (int)((g->color_top >> shift) & 0xFF), c1 = (int)((g->color_bottom >> shift) & 0xFF);

        out |= (uint32_t)(c0 + (int)((float)(c1 - c0) * f)) << shift;
    }
    return out;
}

static void
rr_quad(float x0, float x1, float y0, float y1, float z, const rr_fill_t* g) {
    pvr_vertex_t vert = {.argb = 0, .oargb = 0, .flags = PVR_CMD_VERTEX, .z = z, .u = 0, .v = 0};
    const float xs[4] = {x0, x1, x0, x1};
    const float ys[4] = {y0, y0, y1, y1};

    for (int k = 0; k < 4; k++) {
        vert.flags = k == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vert.x = xs[k];
        vert.y = ys[k];
        vert.argb = rr_color_at(g, ys[k]);
        pvr_prim(&vert, sizeof(vert));
    }
}

/* How far the shape's left end is moved in at the middle of a pixel row, for a rectangle (y .. y+h) with corner radii rt (top) and rb. */
static float
rr_inset(float yc, float y, float h, float rt, float rb) {
    float r = 0.0f, dy = 0.0f;

    if (yc < y + rt) {
        r = rt;
        dy = y + rt - yc;
    } else if (yc > y + h - rb) {
        r = rb;
        dy = yc - (y + h - rb);
    }
    if (r <= 0.5f || dy >= r) {
        return r > 0.5f ? r : 0.0f;
    }
    return (float)(int)(r - sqrtf(r * r - dy * dy) + 0.5f);
}

/* A rounded rectangle filled with the gradient g; with bw above zero only the border band of that width. rt and rb are the radii of
 * the top and the bottom corners. */
static void
rr_shape(float x, float y, float w, float h, float rt, float rb, float bw, const rr_fill_t* g) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const float z = z_inc();
    const float rmax = (w < h ? w : h) * 0.5f;
    float run_y = 0.0f, a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
    int run_pieces = 0;

    rt = rt > rmax ? rmax : rt;
    rb = rb > rmax ? rmax : rb;
    pvr_poly_cxt_col(&context, draw_get_list());
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    for (int row = 0; row <= (int)h; row++) {
        const float yc = y + (float)row + 0.5f;
        float n0 = 0.0f, n1 = 0.0f, m0 = 0.0f, m1 = 0.0f;
        int pieces = 0;

        if (row < (int)h) {
            const float o = rr_inset(yc, y, h, rt, rb);
            const float ox0 = x + o, ox1 = x + w - o;

            if (bw <= 0.0f) {
                n0 = ox0;
                n1 = ox1;
                pieces = 1;
            } else if (yc < y + bw || yc > y + h - bw) {
                n0 = ox0;
                n1 = ox1;
                pieces = 1;
            } else {
                const float irt = rt > bw ? rt - bw : 0.0f, irb = rb > bw ? rb - bw : 0.0f;
                const float i = rr_inset(yc, y + bw, h - 2.0f * bw, irt, irb);

                n0 = ox0;
                n1 = x + bw + i;
                m0 = x + w - bw - i;
                m1 = ox1;
                pieces = 2;
            }
        }
        if (pieces == run_pieces && (pieces == 0 || (n0 == a0 && n1 == a1 && (pieces == 1 || (m0 == b0 && m1 == b1))))) {
            continue;
        }
        if (run_pieces > 0) {
            const float y1 = y + (float)row;

            rr_quad(a0, a1, run_y, y1, z, g);
            if (run_pieces == 2) {
                rr_quad(b0, b1, run_y, y1, z, g);
            }
        }
        run_y = y + (float)row;
        run_pieces = pieces;
        a0 = n0;
        a1 = n1;
        b0 = m0;
        b1 = m1;
    }
}

/* A popup's frame: a 2 px border, the fill, and with header_height above zero a header bar in the border colour. */
void
draw_draw_popup_frame(int x, int y, int width, int height, int header_height, uint32_t border_color, uint32_t fill_color) {
    float r = (float)popup_corner_radius;

    if (header_height > 0 && r > (float)header_height * 0.5f) {
        r = (float)header_height * 0.5f; /* the header's corners must match the fill's */
    }

    if (popup_corner_radius == 0) {
        draw_draw_quad(x - 2, y - 2, (float)(width + 4), (float)(height + 4), border_color);
        draw_draw_quad(x, y, (float)width, (float)height, fill_color);
        if (header_height > 0) {
            draw_draw_quad(x, y, (float)width, (float)header_height, border_color);
        }
        return;
    }
    {
        const rr_fill_t border = {(float)y, (float)(y + height), border_color, border_color};
        const rr_fill_t fill = {(float)y, (float)(y + height), fill_color, fill_color};

        rr_shape((float)(x - 2), (float)(y - 2), (float)(width + 4), (float)(height + 4), r + 2.0f, r + 2.0f, 0.0f, &border);
        rr_shape((float)x, (float)y, (float)width, (float)height, r, r, 0.0f, &fill);
        if (header_height > 0) {
            rr_shape((float)x, (float)y, (float)width, (float)header_height, r, 0.0f, 0.0f, &border);
        }
    }
}

/* A glass panel for backdrop themes: a translucent fill that is a little lighter at the top, and a border. Colours are 0xRRGGBB;
 * alpha (0..255) is the fill's opacity at the top, the bottom is a third more transparent. */
void
draw_draw_panel(int x, int y, int width, int height, int radius, int border_width, uint32_t border_rgb, uint32_t fill_rgb, int alpha) {
    const float r = (float)radius;
    const uint32_t rgb = fill_rgb & 0x00FFFFFFu;
    const uint32_t light = (((rgb >> 16) & 0xFF) * 3 / 2 > 255 ? 255 : ((rgb >> 16) & 0xFF) * 3 / 2) << 16
                           | (((rgb >> 8) & 0xFF) * 3 / 2 > 255 ? 255 : ((rgb >> 8) & 0xFF) * 3 / 2) << 8
                           | (((rgb & 0xFF) * 3 / 2 > 255 ? 255 : (rgb & 0xFF) * 3 / 2));
    const float bw = (float)border_width;
    /* The fill only inside the border: the translucent fill is not drawn twice where the border is. */
    const rr_fill_t fill = {(float)y, (float)(y + height), ((uint32_t)alpha << 24) | light,
                            ((uint32_t)(alpha * 17 / 20) << 24) | rgb};
    const rr_fill_t border = {(float)y, (float)(y + height), 0xFF000000u | border_rgb, 0xFF000000u | border_rgb};
    const float ri = r > bw ? r - bw : 0.0f;

    rr_shape((float)x + bw, (float)y + bw, (float)width - 2.0f * bw, (float)height - 2.0f * bw, ri, ri, 0.0f, &fill);
    rr_shape((float)x, (float)y, (float)width, (float)height, r, r, bw, &border);
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
