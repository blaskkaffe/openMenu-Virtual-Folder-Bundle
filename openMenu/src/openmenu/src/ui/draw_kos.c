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
#include "ui/backdrop_texture.h"

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
void
draw_backdrop(uint32_t tint) {
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
                v->u = (float)k * (1.0f / CYL_SEGMENTS);
                v->v = 1.0f - yl * (1.0f / CYL_HEIGHT);
                scene_colour(v, opacity, 1.0f);
            }
        }
    }

    scene_header(0);
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

void
draw_backdrop_scene(int low_res, int clouds_percent) {
    const float t = (float)backdrop_frame * 0.016f; /* the page adds 0.016 a frame */

    backdrop_frame++;
    if (!backdrop_texture()) {
        return;
    }
    draw_scene_cylinder(backdrop_frame, clouds_percent > 0 ? (float)clouds_percent * 0.01f : 1.0f);
    /* 24 x 22 cells normally, 16 x 16 for a low-res theme (THEME.INI backdrop=2): about half the triangles */
    draw_scene_plane(t, low_res ? 16 : PLANE_COLS, low_res ? 16 : PLANE_ROWS);
}

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
