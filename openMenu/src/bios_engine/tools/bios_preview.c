/*
 * bios_preview: render the BIOS-style menu to a PPM image on the PC, using the
 * same engine code as the console build and a software rasteriser instead of
 * the PVR. For checking models, scripts and projection without hardware.
 *
 *   bios_preview dc_boot.bin out.ppm [frames=120] [selected=0] [script=-1]
 *
 * Set BIOS_PREVIEW_LOGO=LOGO.PVR to see a replacement header logo.
 *
 * With script >= 0 only that single script is run as an object (useful to look
 * at one model); otherwise the full main menu is shown.
 * Text surfaces are shown as outlines: the console draws them with the BIOS font.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios_menu.h"
#include "bios_page.h"
#include "bios_list.h"
#include "tex_decode.h"

#define W 640
#define H 480

static float fb[H][W][3];

typedef struct {
    uint32_t* px;
    int w, h;
} bitmap;

typedef struct {
    bscene_texref ref;
    bitmap bmp;
    int valid;
} cached_tex;

static cached_tex cache[256];
static int cache_n;
static const bios_rom* g_rom;

static bitmap logo_bmp; /* optional replacement of the header logo, see biologo.py */

static const bitmap*
lookup(bscene_texref ref) {
    if (logo_bmp.px && ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        return &logo_bmp;
    }
    for (int i = 0; i < cache_n; i++) {
        if (cache[i].ref.kind == ref.kind && cache[i].ref.a == ref.a && cache[i].ref.b == ref.b) {
            return cache[i].valid ? &cache[i].bmp : NULL;
        }
    }
    if (cache_n >= 256) {
        return NULL;
    }
    cached_tex* c = &cache[cache_n++];
    c->ref = ref;
    bios_texture t;
    int ok = (ref.kind == BSCENE_TEX_GBIX) ? bios_texture_find(g_rom, (uint32_t)ref.a, &t) == 0
                                            : bios_texlist_texture(g_rom, ref.a >= 0x1000 ? 61 : ref.a, ref.b, &t) == 0;
    if (ok) {
        c->bmp.w = t.width;
        c->bmp.h = t.height;
        c->bmp.px = malloc(sizeof(uint32_t) * t.width * t.height);
        c->valid = c->bmp.px && bios_texture_decode(&t, c->bmp.px) == 0;
    }
    return c->valid ? &c->bmp : NULL;
}

static void
tri(void* user, const bscene_vtx v[3], bscene_texref ref) {
    (void)user;
    const bitmap* tex = ref.kind != BSCENE_TEX_NONE ? lookup(ref) : NULL;
    float minx = fminf(v[0].x, fminf(v[1].x, v[2].x)), maxx = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
    float miny = fminf(v[0].y, fminf(v[1].y, v[2].y)), maxy = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
    int x0 = (int)fmaxf(minx, 0), x1 = (int)fminf(maxx + 1, W - 1), y0 = (int)fmaxf(miny, 0), y1 = (int)fminf(maxy + 1, H - 1);
    float den = (v[1].y - v[2].y) * (v[0].x - v[2].x) + (v[2].x - v[1].x) * (v[0].y - v[2].y);
    if (fabsf(den) < 1e-9f) {
        return;
    }
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((v[1].y - v[2].y) * (px - v[2].x) + (v[2].x - v[1].x) * (py - v[2].y)) / den;
            float w1 = ((v[2].y - v[0].y) * (px - v[2].x) + (v[0].x - v[2].x) * (py - v[2].y)) / den;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            float iw = w0 * v[0].invw + w1 * v[1].invw + w2 * v[2].invw;
            float col[4];
            for (int c = 0; c < 4; c++) {
                int sh = 24 - 8 * c;
                col[c] = (w0 * (float)((v[0].argb >> sh) & 255) * v[0].invw + w1 * (float)((v[1].argb >> sh) & 255) * v[1].invw
                          + w2 * (float)((v[2].argb >> sh) & 255) * v[2].invw) / iw / 255.0f;
            }
            if (tex) {
                float u = (w0 * v[0].u * v[0].invw + w1 * v[1].u * v[1].invw + w2 * v[2].u * v[2].invw) / iw;
                float t = (w0 * v[0].v * v[0].invw + w1 * v[1].v * v[1].invw + w2 * v[2].v * v[2].invw) / iw;
                int tx = (int)floorf(u * tex->w) % tex->w, ty = (int)floorf(t * tex->h) % tex->h;
                if (tx < 0) tx += tex->w;
                if (ty < 0) ty += tex->h;
                uint32_t p = tex->px[ty * tex->w + tx];
                col[0] *= (float)(p >> 24) / 255.0f;
                col[1] *= (float)((p >> 16) & 255) / 255.0f;
                col[2] *= (float)((p >> 8) & 255) / 255.0f;
                col[3] *= (float)(p & 255) / 255.0f;
            }
            for (int c = 0; c < 3; c++) {
                fb[y][x][c] = fb[y][x][c] * (1.0f - col[0]) + col[c + 1] * col[0];
            }
        }
    }
}

static void
text(void* user, const bvm_obj* o, float x, float y, float invw) {
    (void)user; (void)invw;
    int x0 = (int)(x - o->text_w / 2.0f), y0 = (int)(y - o->text_h / 2.0f), x1 = x0 + o->text_w, y1 = y0 + o->text_h; /* anchor = centre */
    for (int i = x0; i < x1; i++) {
        for (int k = 0; k < 2; k++) {
            int yy = k ? y1 - 1 : y0;
            if (i >= 0 && i < W && yy >= 0 && yy < H) fb[yy][i][0] = fb[yy][i][1] = fb[yy][i][2] = 1.0f;
        }
    }
    for (int j = y0; j < y1; j++) {
        for (int k = 0; k < 2; k++) {
            int xx = k ? x1 - 1 : x0;
            if (j >= 0 && j < H && xx >= 0 && xx < W) fb[j][xx][0] = fb[j][xx][1] = fb[j][xx][2] = 1.0f;
        }
    }
}

static void
demo_row(void* user, int index, bpage_row* out) {
    (void)user;
    out->icon = index < 4 ? BPAGE_ICON_BIOS(index) : BPAGE_ICON_DIGIT(index - 4);
}

int
main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s dc_boot.bin out.ppm [frames] [selected] [script]\n", argv[0]);
        return 2;
    }
    int frames = argc > 3 ? atoi(argv[3]) : 120, selected = argc > 4 ? atoi(argv[4]) : 0, script = argc > 5 ? atoi(argv[5]) : -1;

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static uint8_t rom_data[BIOS_ROM_SIZE];
    size_t got = fread(rom_data, 1, sizeof(rom_data), f);
    fclose(f);
    static bios_rom rom;
    int err = bios_rom_init(&rom, rom_data, got);
    if (err != BIOS_ROM_OK) {
        fprintf(stderr, "not a supported boot ROM (error %d)\n", err);
        return 1;
    }
    g_rom = &rom;

    const char* logo_path = getenv("BIOS_PREVIEW_LOGO");
    if (logo_path && *logo_path) {
        FILE* lf = fopen(logo_path, "rb");
        static uint8_t logo_file[256 * 1024];
        size_t n = lf ? fread(logo_file, 1, sizeof(logo_file), lf) : 0;
        if (lf) {
            fclose(lf);
        }
        bios_texture lt;
        if (bios_texture_parse(logo_file, n, &lt) == 0) {
            logo_bmp.w = lt.width;
            logo_bmp.h = lt.height;
            logo_bmp.px = malloc(sizeof(uint32_t) * lt.width * lt.height);
            if (!logo_bmp.px || bios_texture_decode(&lt, logo_bmp.px) != 0) {
                logo_bmp.px = NULL;
            }
        }
        if (!logo_bmp.px) {
            fprintf(stderr, "could not use %s as logo\n", logo_path);
        }
    }

    static bmenu menu;
    static bpage page;
    static blist list;
    bmenu_init(&menu, &rom, NULL);
    if (script == -2) { /* settings page demo: `selected` = cursor row, 10 rows */
        bpage_open(&page, &menu, 10);
        for (int i = 0; i < selected; i++) {
            bpage_move(&page, 1);
        }
    } else if (script == -3 || script == -4) { /* game list demo: 7 rows; -4 = launch animation at `frames` */
        blist_open(&list, &menu, 7, 30);
        blist_goto(&list, selected);
    } else if (script >= 0) {
        bvm_obj* o = bvm_create(&menu.vm, script, 0x400, 0x2000);
        if (!o) {
            fprintf(stderr, "script %d is unused\n", script);
            return 1;
        }
        o->pos_tw[2].cur = -340.0f; /* scripts that do not place themselves */
    } else {
        bmenu_show_main(&menu, selected);
    }
    for (int i = 0; i < frames; i++) {
        bmenu_update(&menu);
        if (script == -2) {
            bpage_sync(&page, demo_row, NULL);
        }
        if (script == -3 || script == -4) {
            if (script == -4 && i == 20) {
                blist_launch_start(&list);
            }
            blist_launch_step(&list);
            blist_sync(&list);
        }
    }
    if (menu.vm.error) {
        fprintf(stderr, "script error %d at %#x\n", menu.vm.error, menu.vm.error_pc);
    }

    uint32_t top, bottom;
    dcbg_gradient(&menu.bg, &top, &bottom);
    for (int y = 0; y < H; y++) {
        float t = (float)y / (H - 1);
        for (int c = 0; c < 3; c++) {
            int sh = 16 - 8 * c;
            float a = (float)((top >> sh) & 255), b = (float)((bottom >> sh) & 255);
            for (int x = 0; x < W; x++) {
                fb[y][x][c] = (a + (b - a) * t) / 255.0f;
            }
        }
    }
    bscene_sink sink = {NULL, tri, text};
    if (script == -3 || script == -4) {
        bscene_draw_background(&menu.bg, &sink);
        blist_draw(&list, &sink);
    } else if (script == -2) {
        bscene_draw_background(&menu.bg, &sink);
        bpage_draw(&page, &sink);
    } else {
        bmenu_draw(&menu, &sink);
    }

    FILE* out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }
    fprintf(out, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            for (int c = 0; c < 3; c++) {
                float v = fb[y][x][c];
                fputc((int)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5f), out);
            }
        }
    }
    fclose(out);
    printf("wrote %s (%d frames)\n", argv[2], frames);
    return 0;
}
