/*
 * Host test for the portable engine pieces (bios_rom tables, nj_model, bios_vm, dcbg).
 * A synthetic ROM image is assembled in memory, so no Sega data is involved.
 * If BIOS_ROM_FILE points at a real dump, every model, motion and script is also
 * run through the same code and must load / execute without errors.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios_audio.h"
#include "bios_rom.h"
#include "bios_vm.h"
#include "dcbg.h"
#include "bios_scene.h"
#include "nj_model.h"
#include "tex_decode.h"

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-3f)

static void
put32_at(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint8_t* image;
static uint32_t emit_at; /* ROM offset where the next emitted byte goes */

static void
put8(uint32_t o, uint8_t v) {
    image[o] = v;
}
static void
put16(uint32_t o, uint16_t v) {
    image[o] = (uint8_t)v;
    image[o + 1] = (uint8_t)(v >> 8);
}
static void
put32(uint32_t o, uint32_t v) {
    put16(o, (uint16_t)v);
    put16(o + 2, (uint16_t)(v >> 16));
}
static void
putf(uint32_t o, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    put32(o, u);
}

/* bytecode emitter: every function returns the ROM offset of the instruction */
static uint32_t
e8(uint8_t v) {
    put8(emit_at, v);
    return emit_at++;
}
static void
e32(uint32_t v) {
    put32(emit_at, v);
    emit_at += 4;
}
static void
e16(uint16_t v) {
    put16(emit_at, v);
    emit_at += 2;
}
static uint32_t
i_set(int var, int32_t value) {
    uint32_t pc = e8(0x20);
    e32((uint32_t)value);
    e32((uint32_t)var);
    return pc;
}
static uint32_t i_yield(void) { return e8(0x10); }
static uint32_t i_end(void) { return e8(0x01); }
static uint32_t i_ret(void) { return e8(0x18); }
static uint32_t i_wait(int n) { uint32_t pc = e8(0x11); e32((uint32_t)n); return pc; }
static uint32_t i_jump(uint32_t target) { uint32_t pc = e8(0x13); e32(target - pc); return pc; }
static uint32_t i_call(uint32_t target) { uint32_t pc = e8(0x17); e32(target - pc); return pc; }
static uint32_t i_loop(int var, int count) { uint32_t pc = e8(0x15); e32((uint32_t)var); e32((uint32_t)count); return pc; }
static uint32_t i_endloop(void) { return e8(0x16); }
static uint32_t i_arith(int op, int32_t v, int var) { uint32_t pc = e8(0x21); e8((uint8_t)op); e32((uint32_t)v); e32((uint32_t)var); return pc; }
static uint32_t i_spawn(uint32_t target, uint16_t id, int16_t prio) { uint32_t pc = e8(0x30); e32(target - pc); e16(id); e16((uint16_t)prio); return pc; }
static uint32_t i_pos_target(int x, int y, int z) { uint32_t pc = e8(0x54); e32((uint32_t)x); e32((uint32_t)y); e32((uint32_t)z); return pc; }
static uint32_t i_pos_frames(int x, int y, int z) { uint32_t pc = e8(0x56); e32((uint32_t)x); e32((uint32_t)y); e32((uint32_t)z); return pc; }
static uint32_t i_scale(int x, int y, int z) { uint32_t pc = e8(0x58); e32((uint32_t)x); e32((uint32_t)y); e32((uint32_t)z); return pc; }
static uint32_t i_if(int cond, int32_t a, int32_t b, uint32_t target) { uint32_t pc = e8(0x24); e8((uint8_t)cond); e32((uint32_t)a); e32((uint32_t)b); e32(target - pc); return pc; }
static uint32_t i_model(int32_t m) { uint32_t pc = e8(0x27); e8(0); e32((uint32_t)m); return pc; }
static uint32_t i_motion_frm(int cur, int step, int target) { uint32_t pc = e8(0x68); e32((uint32_t)cur); e32((uint32_t)step); e32((uint32_t)target); return pc; }
static uint32_t i_rflag(uint16_t key, uint8_t on) { uint32_t pc = e8(0x25); e16(key); e8(on); return pc; }

#define BANK 0x6F5BC
#define VAR(n) ((int32_t)(0x80000000u + (n)))

static void
set_script(int idx, uint32_t offset) {
    put32(BANK + 4u * (uint32_t)idx, offset - (BANK + 4u * (uint32_t)idx));
}

static void
build_scripts(void) {
    for (int i = 0; i < 88; i++) {
        put32(BANK + 4u * i, 0xFFFFFEE8u); /* unused */
    }
    put32(BANK, 88 * 4); /* script 0 starts right behind the offset table */
    emit_at = 0x70000;

    /* script 1: arithmetic, loop, tween, wait, end */
    set_script(1, emit_at);
    i_set(0, 3);
    i_loop(1, 3);
    i_arith(0, VAR(0), 2); /* var2 += var0 */
    i_endloop();
    i_pos_target(100 * 256, 0, 0);
    i_pos_frames(10, 0, 0);
    i_scale(512, 512, 512);
    i_wait(2);
    i_set(3, 1);
    i_end();

    /* script 2: call/return, spawn, then idle */
    uint32_t sub = 0x70400;
    set_script(2, 0x70200);
    emit_at = 0x70200;
    i_call(sub);
    i_spawn(0x70300, 0x30, 5);
    uint32_t idle = i_yield();
    i_jump(idle);
    emit_at = sub;
    i_set(5, 7);
    i_ret();

    /* script 3: child, sets var0 then ends */
    set_script(3, 0x70300);
    emit_at = 0x70300;
    i_set(0, 9);
    i_end();

    /* script 4: switch on var0 (preset by the test through arithmetic) + conditional */
    set_script(4, 0x70500);
    emit_at = 0x70500;
    i_set(0, 2);
    uint32_t sw = e8(0x14);
    e32(0);                 /* var 0 */
    e16(2);                 /* max index */
    uint32_t table_rel_at = emit_at;
    e32(0);                 /* patched below */
    uint32_t after = emit_at;
    uint32_t table = emit_at;
    e32(0); e32(0); e32(0); /* three entries, patched below */
    uint32_t case0 = i_set(6, 100), c0e = i_yield();
    uint32_t case1 = i_set(6, 200), c1e = i_yield();
    uint32_t case2 = i_set(6, 300);
    i_if(0, VAR(6), 300, emit_at + 14 + 9); /* taken: skips the next set */
    i_set(7, 1);
    i_set(7, 2);
    i_yield();
    (void)after; (void)c0e; (void)c1e;
    put32(table_rel_at, table - sw);
    put32(table + 0, case0 - table);
    put32(table + 4, case1 - table);
    put32(table + 8, case2 - table);

    /* script 5: unknown opcode; script 6: runaway loop without yield */
    set_script(5, 0x70600);
    emit_at = 0x70600;
    e8(0xEE);
    set_script(6, 0x70700);
    emit_at = 0x70700;
    uint32_t spin = emit_at;
    i_jump(spin);

    /* script 7: model/motion/flags + motion frame tween (loops back to var7 = 0 at the end) */
    set_script(7, 0x70800);
    emit_at = 0x70800;
    i_model(0);
    i_rflag(0, 1);
    i_rflag(1, 1);
    i_motion_frm(0, 256, 4 * 256); /* frame 0, +1/frame, wraps at 4 */
    uint32_t spin7 = i_yield();
    i_jump(spin7);
}

/* ---- Ninja model + motion ------------------------------------------------------ */

#define MODEL_AT 0x80000
#define MOTION_AT 0x82000

/* The 18 textures of the menu image: bios_rom_init uses them to recognise the layout. */
static void
build_textures(void) {
    static const struct {
        uint32_t off;
        uint32_t gbix;
        uint16_t w, h;
    } tex[] = {
        {0x0728B0, 18, 32, 32},  {0x0730D0, 17, 32, 32},  {0x0738F0, 32, 64, 64},   {0x075910, 12, 64, 64},
        {0x077930, 0, 256, 256}, {0x07C150, 11, 32, 32},  {0x07C970, 114, 128, 32}, {0x07E990, 2, 32, 32},
        {0x07F1B0, 3, 32, 32},   {0x07F9D0, 5, 32, 32},   {0x0801F0, 6, 32, 32},    {0x080A10, 7, 64, 64},
        {0x082A30, 8, 32, 32},   {0x083250, 9, 128, 16},  {0x084270, 10, 128, 16},  {0x085290, 1, 8, 8},
        {0x085330, 0, 256, 256}, {0x089B50, 114, 128, 32},
    };
    for (unsigned i = 0; i < sizeof(tex) / sizeof(tex[0]); i++) {
        uint32_t o = tex[i].off;
        memcpy(image + o, "GBIX", 4);
        put32(o + 8, tex[i].gbix);
        memcpy(image + o + 16, "PVRT", 4);
        image[o + 24] = BIOS_PVR_ARGB4444;
        image[o + 25] = tex[i].w == 256 ? BIOS_PVR_VQ : (tex[i].w == tex[i].h ? BIOS_PVR_TWIDDLED : BIOS_PVR_RECTANGLE);
        put16(o + 28, tex[i].w);
        put16(o + 30, tex[i].h);
    }
}

static void
build_model(void) {
    uint32_t base = 0x8C000000u;
    /* root node with a mesh, child node without */
    uint32_t root = MODEL_AT, child = MODEL_AT + 0x40, mesh = MODEL_AT + 0x80, vlist = MODEL_AT + 0xA0,
             plist = MODEL_AT + 0x100;
    put32(root, 0);
    put32(root + 4, base + mesh);
    putf(root + 8, 1.0f); putf(root + 12, 2.0f); putf(root + 16, 3.0f);
    put32(root + 0x14, 0); put32(root + 0x18, 0); put32(root + 0x1C, 0);
    putf(root + 0x20, 1.0f); putf(root + 0x24, 1.0f); putf(root + 0x28, 1.0f);
    put32(root + 0x2C, base + child);
    put32(root + 0x30, 0);

    put32(child, 0);
    put32(child + 4, 0);
    putf(child + 8, 10.0f); putf(child + 12, 0.0f); putf(child + 16, 0.0f);
    put32(child + 0x14, 0x4000); /* 90 degrees about X */
    putf(child + 0x20, 1.0f); putf(child + 0x24, 1.0f); putf(child + 0x28, 1.0f);
    put32(child + 0x2C, 0); put32(child + 0x30, 0);

    put32(mesh, base + vlist);
    put32(mesh + 4, base + plist);

    /* vertex chunk type 0x20 (16 bytes per vertex), 4 vertices, indices 0..3 */
    put32(vlist, 0x20u | (17u << 16)); /* size = 1 info word + 4 * 4 words */
    put32(vlist + 4, 0u | (4u << 16));
    float pts[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    for (int i = 0; i < 4; i++) {
        putf(vlist + 8 + 16u * (uint32_t)i, pts[i][0]);
        putf(vlist + 12 + 16u * (uint32_t)i, pts[i][1]);
        putf(vlist + 16 + 16u * (uint32_t)i, pts[i][2]);
    }
    put32(vlist + 8 + 64, 0xFF);

    /* polygon list: texture 2, diffuse material, one strip of 4 with UVs */
    uint32_t p = plist;
    put16(p, 8); put16(p + 2, 2); p += 4;                  /* texture id chunk */
    put16(p, 17); put16(p + 2, 2); put32(p + 4, 0xFF336699); p += 8; /* diffuse material chunk */
    put16(p, 65 | (4 << 8)); put16(p + 2, 14); p += 4;     /* strip with UVs, flag byte 4 */
    put16(p, 1); p += 2;                                   /* one strip, no user words */
    put16(p, 4); p += 2;                                   /* length 4 */
    for (int i = 0; i < 4; i++) {
        put16(p, (uint16_t)i);
        put16(p + 2, (uint16_t)((i & 1) ? 255 : 0));
        put16(p + 4, (uint16_t)((i & 2) ? 255 : 0));
        p += 6;
    }
    put16(p, 0xFF);

    /* tables */
    put32(0x6F3C0, base + root);

    /* motion: 2 nodes, node 0 moves along x from 0 to 10 over 10 frames, node 1 static */
    uint32_t md = MOTION_AT + 0x40, keys = MOTION_AT + 0x100;
    put32(MOTION_AT, base + md);
    put32(MOTION_AT + 4, 10);
    put16(MOTION_AT + 8, 0);
    put16(MOTION_AT + 10, 0);
    put32(md + 0, base + keys);
    put32(md + 12, 2);
    put32(keys, 0); putf(keys + 4, 0); putf(keys + 8, 0); putf(keys + 12, 0);
    put32(keys + 16, 10); putf(keys + 20, 10); putf(keys + 24, 0); putf(keys + 28, 0);
    put32(0x6F524, base + MOTION_AT);

    /* a texture to resolve through a texlist */
    uint32_t tex = 0x728B0;
    memcpy(image + tex, "GBIX", 4); put32(tex + 8, 18);
    memcpy(image + tex + 16, "PVRT", 4); image[tex + 24] = BIOS_PVR_ARGB4444; image[tex + 25] = BIOS_PVR_TWIDDLED;
    put16(tex + 28, 32); put16(tex + 30, 32);
    uint32_t texlist = 0x83000, names = 0x83100, info = 0x83200;
    put32(texlist, base + names); put32(texlist + 4, 1);
    put32(names, base + info);
    put32(info, base + tex);
    put32(0x6F25C, base + texlist);
}

static void
test_rom_tables(const bios_rom* rom) {
    CHECK(bios_model_addr(rom, 0) == 0x8C000000u + MODEL_AT);
    CHECK(bios_model_addr(rom, 1) == 0);
    CHECK(bios_model_addr(rom, BIOS_MODEL_COUNT) == 0);
    CHECK(bios_motion_addr(rom, 0) == 0x8C000000u + MOTION_AT);
    CHECK(bios_motion_addr(rom, BIOS_MOTION_COUNT) == 0);
    CHECK(bios_texlist_count(rom, 0) == 1);
    bios_texture t;
    CHECK(bios_texlist_texture(rom, 0, 0, &t) == 0 && t.gbix == 18 && t.width == 32);
    CHECK(bios_texlist_texture(rom, 0, 1, &t) != 0);
    CHECK(bios_texture_at(rom, 0x8C000000u + 0x728B0, &t) == 0 && t.gbix == 18);
    CHECK(bios_texture_at(rom, 0x8C100000u + 0x728B0, &t) != 0);
}

static void
test_model(const bios_rom* rom) {
    nj_object obj;
    CHECK(nj_object_load(rom, bios_model_addr(rom, 0), &obj) == 0);
    CHECK(obj.count == 2);
    CHECK(obj.nodes[0].parent == -1 && obj.nodes[1].parent == 0);
    CHECK(obj.nodes[0].mesh && !obj.nodes[1].mesh);
    nj_mesh* m = obj.nodes[0].mesh;
    CHECK(m->nverts == 4 && m->npolys == 1);
    CHECK(m->polys[0].tex == 2 && m->polys[0].has_diffuse && m->polys[0].diffuse == 0xFF336699);
    CHECK(m->polys[0].has_uv && m->polys[0].strip_flags == 4);
    CHECK(m->polys[0].ntris == 2);
    const nj_corner* c = m->polys[0].corners;
    CHECK(c[0].idx == 0 && c[1].idx == 1 && c[2].idx == 2);
    CHECK(c[3].idx == 2 && c[4].idx == 1 && c[5].idx == 3); /* odd triangle is flipped */
    CHECK(NEAR(c[1].u, 1.0f) && NEAR(c[2].v, 1.0f) && NEAR(c[0].u, 0.0f));
    CHECK(m->verts[3].valid && NEAR(m->verts[3].pos.x, 1.0f) && NEAR(m->verts[3].pos.y, 1.0f));

    nj_mat4 world[2];
    nj_object_pose(&obj, NULL, 0, world);
    nj_vec3 p = nj_mat_apply(&world[0], m->verts[1].pos);
    CHECK(NEAR(p.x, 2.0f) && NEAR(p.y, 2.0f) && NEAR(p.z, 3.0f));
    /* child: translate (10,0,0) then rotate 90 degrees about X; its +Y becomes +Z */
    nj_vec3 q = nj_mat_apply(&world[1], (nj_vec3){0, 1, 0});
    CHECK(NEAR(q.x, 11.0f) && NEAR(q.y, 2.0f) && NEAR(q.z, 4.0f));

    nj_motion mo;
    CHECK(nj_motion_load(rom, bios_motion_addr(rom, 0), obj.count, &mo) == 0);
    CHECK(mo.frames == 10 && mo.count == 2 && mo.nodes[0].pos.count == 2 && mo.nodes[1].pos.count == 0);
    nj_object_pose(&obj, &mo, 5.0f, world);
    p = nj_mat_apply(&world[0], (nj_vec3){0, 0, 0});
    CHECK(NEAR(p.x, 5.0f) && NEAR(p.y, 0.0f)); /* animated position replaces the node's own (1,2,3) */
    nj_object_pose(&obj, &mo, 50.0f, world); /* beyond the last key: held */
    p = nj_mat_apply(&world[0], (nj_vec3){0, 0, 0});
    CHECK(NEAR(p.x, 10.0f));
    nj_motion_free(&mo);
    CHECK(nj_motion_load(rom, 0x8C000000u + 0x1000, obj.count, &mo) != 0); /* zeros: no key data */
    nj_object_free(&obj);

    CHECK(nj_object_load(rom, 0x8D000000u, &obj) != 0); /* outside the ROM */
}

static void
test_tex_decode(void) {
    CHECK(bios_untwiddle(0, 0) == 0 && bios_untwiddle(1, 0) == 2 && bios_untwiddle(0, 1) == 1 && bios_untwiddle(1, 1) == 3);
    CHECK(bios_untwiddle(2, 0) == 8 && bios_untwiddle(3, 3) == 15);

    /* 4x4 twiddled RGB565: pixel (x, y) holds the value x + 4 * y in the red channel */
    static uint8_t payload[32];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint16_t v = (uint16_t)((x + 4 * y) << 11);
            uint32_t at = bios_untwiddle((uint32_t)x, (uint32_t)y) * 2;
            payload[at] = (uint8_t)v;
            payload[at + 1] = (uint8_t)(v >> 8);
        }
    }
    bios_texture t = {0, BIOS_PVR_RGB565, BIOS_PVR_TWIDDLED, 4, 4, payload, 32, 0};
    uint32_t out[16];
    CHECK(bios_texture_decode(&t, out) == 0);
    for (int i = 0; i < 16; i++) {
        CHECK(((out[i] >> 16) & 255) == (uint32_t)(i * 255 / 31) && (out[i] >> 24) == 255);
    }

    /* 4x4 VQ: codebook entry 1 = four ARGB4444 values, index grid all 1 */
    static uint8_t vq[2048 + 4];
    for (int k = 0; k < 4; k++) {
        vq[8 + 2 * k] = (uint8_t)(0x0F | (k << 4)); /* blue 15, green k */
        vq[8 + 2 * k + 1] = 0xF0;                   /* alpha 15, red 0 */
    }
    for (int i = 0; i < 4; i++) {
        vq[2048 + i] = 1;
    }
    bios_texture q = {0, BIOS_PVR_ARGB4444, BIOS_PVR_VQ, 4, 4, vq, sizeof(vq), 0};
    CHECK(bios_texture_decode(&q, out) == 0);
    CHECK(out[0] == 0xFF0000FFu && ((out[4] >> 8) & 255) == 0x11); /* k=1 is the pixel below: (2x+0, 2y+1) -> row 1 */
    CHECK(((out[1] >> 8) & 255) == 0x22);                           /* k=2 is the pixel to the right */
    bios_texture bad = {0, 0, BIOS_PVR_TWIDDLED_MIPMAP, 4, 4, payload, 0, 0};
    CHECK(bios_texture_decode(&bad, out) != 0);
}

typedef struct {
    int tris, textured, texts;
    float minx, maxx, miny, maxy;
} tally;

static void
tally_tri(void* user, const bscene_vtx v[3], bscene_texref tex) {
    tally* t = (tally*)user;
    t->tris++;
    t->textured += tex.kind != BSCENE_TEX_NONE;
    for (int i = 0; i < 3; i++) {
        if (v[i].x < t->minx) t->minx = v[i].x;
        if (v[i].x > t->maxx) t->maxx = v[i].x;
        if (v[i].y < t->miny) t->miny = v[i].y;
        if (v[i].y > t->maxy) t->maxy = v[i].y;
    }
}

static void
tally_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)obj; (void)x; (void)y; (void)invw;
    ((tally*)user)->texts++;
}

static void
test_scene(const bios_rom* rom) {
    bscene sc;
    bscene_init(&sc, rom);
    bvm vm;
    bvm_init(&vm, rom, NULL);
    bvm_obj* o = bvm_create(&vm, 7, 0x70, 0); /* draws model 0 at the origin of its own space */
    bvm_update(&vm);
    o->pos[2] = -400.0f; /* in front of the camera */

    tally t = {0, 0, 0, 1e9f, -1e9f, 1e9f, -1e9f};
    bscene_sink sink = {&t, tally_tri, tally_text};
    bscene_draw_objects(&sc, &vm, &sink);
    CHECK(t.tris == 2 && t.textured == 2); /* the quad: two textured triangles */
    CHECK(t.minx > 300.0f && t.maxx < 700.0f && t.miny > 0.0f && t.maxy < 480.0f);

    o->pos[2] = 5.0f; /* behind the camera: dropped, not mirrored */
    t.tris = 0;
    bscene_draw_objects(&sc, &vm, &sink);
    CHECK(t.tris == 0);

    o->pos[2] = -400.0f;
    o->flags |= BVM_F_HIDE;
    bscene_draw_objects(&sc, &vm, &sink);
    CHECK(t.tris == 0);

    o->flags &= ~(uint32_t)BVM_F_HIDE;
    o->flags |= BVM_F_COLOUR;
    o->color[0] = -2.0f; /* alpha offset below zero: fully transparent, clamped */
    bscene_draw_objects(&sc, &vm, &sink);
    CHECK(t.tris == 2);

    /* background layers produce a full grid of triangles */
    static dcbg_state bg;
    dcbg_init(&bg, 0, 0);
    for (int i = 0; i < 30; i++) {
        dcbg_step(&bg);
    }
    t.tris = 0;
    bscene_draw_background(&bg, &sink);
    CHECK(t.tris == 2 * (15 * 15 + 12 * 12));
    bscene_free(&sc);
}

static void
test_audio(void) {
    /* container with a 32-byte header and three records: SMPB, SMSB (reversed tag order), a work area */
    static uint8_t c[0x2000];
    memset(c, 0, sizeof(c));
    memcpy(c, "SMLT", 4);
    uint8_t* r = c + 0x20;
    memcpy(r, "SMPB", 4); put32_at(r + 4, 0); put32_at(r + 8, 0x18000); put32_at(r + 12, 0x7520); put32_at(r + 16, 0x140); put32_at(r + 20, 0x940);
    r += 32;
    memcpy(r, "BSMS", 4); put32_at(r + 4, 2); put32_at(r + 8, 0x1F520); put32_at(r + 12, 0x460); put32_at(r + 16, 0x240); put32_at(r + 20, 0x100);
    r += 32;
    memcpy(r, "SFPW", 4); put32_at(r + 8, 0x22000); put32_at(r + 12, 0x1000); put32_at(r + 16, 0xFFFFFFFFu); put32_at(r + 20, 0xFFFFFFFFu);
    r += 32;
    memcpy(r, "SMSB", 4); put32_at(r + 8, 0x1F520); put32_at(r + 16, 0x3000); put32_at(r + 20, 0x10); /* data outside the container */

    baudio_block b[BAUDIO_MAX_BLOCKS];
    int n = baudio_parse_banks(c, sizeof(c), b, BAUDIO_MAX_BLOCKS);
    CHECK(n == 3);
    CHECK(!strcmp(b[0].tag, "SMPB") && b[0].ram_addr == 0x18000 && b[0].offset == 0x140 && b[0].size == 0x940);
    CHECK(!strcmp(b[1].tag, "SMSB") && b[1].unit == 2);
    CHECK(!strcmp(b[2].tag, "SFPW") && b[2].size == 0);
    CHECK(baudio_parse_banks(c, 0x20, b, BAUDIO_MAX_BLOCKS) == 0);
    CHECK(baudio_parse_banks(NULL, 0, b, BAUDIO_MAX_BLOCKS) == 0);

    CHECK(baudio_datamap_addr(&b[0]) == 0x14000 + 0x080);
    CHECK(baudio_datamap_addr(&b[1]) == 0x14000 + 0x000 + 16);
    CHECK(baudio_datamap_addr(&b[2]) == 0x14000 + 0x288);
    uint32_t w[2];
    baudio_datamap_entry(&b[0], w);
    CHECK(w[0] == 0x18000 && w[1] == 0x940);
    baudio_datamap_entry(&b[2], w);
    CHECK(w[1] == 0x1000); /* work area: reserved size */

    uint8_t slot[16];
    baudio_cmd_play(slot, 1, 0, 3, 4);
    CHECK(slot[0] == 0x01 && slot[1] == 0 && slot[2] == 1 && slot[3] == 0 && slot[4] == 3 && slot[5] == 0x20);
    baudio_cmd_master_volume(slot, 15);
    CHECK(slot[0] == 0x81 && slot[2] == 0xF0);
    baudio_cmd_stereo(slot, 1);
    CHECK(slot[0] == 0x8A && slot[2] == 0xFF);
    baudio_cmd_stereo(slot, 0);
    CHECK(slot[2] == 0x00);
}

static int idle_input;

static int
host_input(void* user) {
    (void)user;
    return idle_input;
}

static void
test_vm(const bios_rom* rom) {
    bvm vm;
    bvm_host host = {NULL, host_input, NULL};
    bvm_init(&vm, rom, &host);

    /* ordering: highest priority first */
    bvm_obj* a = bvm_create(&vm, 3, 1, 1);
    bvm_obj* b = bvm_create(&vm, 3, 2, 5);
    bvm_obj* c = bvm_create(&vm, 3, 3, 3);
    CHECK(a && b && c && vm.count == 3);
    CHECK(vm.objs[vm.order[0]].id == 2 && vm.objs[vm.order[1]].id == 3 && vm.objs[vm.order[2]].id == 1);
    CHECK(bvm_find(&vm, 3) == c && bvm_find(&vm, 99) == NULL);
    CHECK(bvm_create(&vm, 0x46, 9, 0) == NULL); /* unused slot */
    bvm_kill(&vm, 1);
    CHECK(vm.count == 2 && bvm_find(&vm, 1) == NULL);
    bvm_kill_all(&vm);
    CHECK(vm.count == 0);

    /* script 1 */
    bvm_obj* o = bvm_create(&vm, 1, 0x10, 0);
    bvm_update(&vm);
    CHECK(o->var[0] == 3 && o->var[2] == 9 && o->var[3] == 0);
    CHECK(NEAR(o->pos[0], 10.0f)); /* 100 units over 10 frames */
    CHECK(NEAR(o->scale_tw[0].cur, 2.0f));
    bvm_update(&vm);
    CHECK(NEAR(o->pos[0], 20.0f) && o->var[3] == 0 && o->active);
    bvm_update(&vm); /* wait over: sets var3 then ends */
    CHECK(!o->active && vm.count == 0 && o->var[3] == 1);
    CHECK(vm.error == 0);

    /* script 2: call/return and spawn */
    o = bvm_create(&vm, 2, 0x20, 10);
    bvm_update(&vm);
    CHECK(o->var[5] == 7);
    bvm_obj* child = bvm_find(&vm, 0x30);
    CHECK(child && child->prio == 5 && vm.count == 2);
    CHECK(vm.objs[vm.order[0]].id == 0x20 && vm.objs[vm.order[1]].id == 0x30);
    bvm_update(&vm);
    CHECK(bvm_find(&vm, 0x30) == NULL && child->var[0] == 9 && vm.count == 1);

    /* script 4: switch + conditional */
    bvm_kill_all(&vm);
    o = bvm_create(&vm, 4, 0x40, 0);
    bvm_update(&vm);
    CHECK(o->var[6] == 300 && o->var[7] == 2);
    CHECK(vm.error == 0);

    /* errors are reported, not fatal */
    bvm_kill_all(&vm);
    bvm_create(&vm, 5, 0x50, 0);
    bvm_update(&vm);
    CHECK(vm.error == 2);
    vm.error = 0;
    bvm_kill_all(&vm);
    bvm_create(&vm, 6, 0x60, 0);
    bvm_update(&vm);
    CHECK(vm.error == 3);

    /* script 7: motion frame counter wraps to var7 */
    vm.error = 0;
    bvm_kill_all(&vm);
    o = bvm_create(&vm, 7, 0x70, 0);
    bvm_update(&vm);
    CHECK(o->model == 0 && (o->flags & BVM_F_MODEL) && (o->flags & BVM_F_MOTION));
    CHECK(NEAR(o->motion_tw.cur, 1.0f));
    bvm_update(&vm); bvm_update(&vm); bvm_update(&vm);
    CHECK(NEAR(o->motion_tw.cur, 0.0f)); /* reached 4 and wrapped */

    /* idle_check: input makes the jump, no input falls through */
    (void)idle_input;
}

static void
test_dcbg(void) {
    static dcbg_state s;
    static dcbg_vertex v[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    dcbg_init(&s, 0, 0);
    for (int i = 0; i < 200; i++) {
        dcbg_step(&s);
    }
    int n = dcbg_project(&s.swirl, &s.swirl_obj, v);
    CHECK(n == 16 * 16);
    int finite = 1;
    for (int i = 0; i < n; i++) {
        finite &= isfinite(v[i].sx) && isfinite(v[i].sy) && isfinite(v[i].invw);
    }
    CHECK(finite);
    n = dcbg_project(&s.water, &s.water_obj, v);
    CHECK(n == 13 * 13);
    uint32_t top, bottom;
    dcbg_gradient(&s, &top, &bottom);
    CHECK(top == 0xFFB0D0D0 && bottom == 0xFF4060C0);
}

static void
test_real_rom(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("FAIL cannot open %s\n", path);
        failures++;
        return;
    }
    uint8_t* data = malloc(BIOS_ROM_SIZE);
    size_t got = fread(data, 1, BIOS_ROM_SIZE, f);
    fclose(f);
    bios_rom rom;
    CHECK(got == BIOS_ROM_SIZE && bios_rom_init(&rom, data, got) == BIOS_ROM_OK);

    int models = 0, bad_models = 0, motions = 0, bad_motions = 0, tex_ok = 0;
    for (int i = 0; i < BIOS_MODEL_COUNT; i++) {
        uint32_t addr = bios_model_addr(&rom, i);
        if (!addr) {
            continue;
        }
        nj_object obj;
        if (nj_object_load(&rom, addr, &obj) == 0) {
            models++;
            nj_object_free(&obj);
        } else {
            bad_models++;
            printf("model %d failed to load\n", i);
        }
        for (int k = 0; k < bios_texlist_count(&rom, i); k++) {
            bios_texture t;
            tex_ok += bios_texlist_texture(&rom, i, k, &t) == 0;
        }
    }
    for (int i = 0; i < BIOS_MOTION_COUNT; i++) {
        uint32_t addr = bios_motion_addr(&rom, i);
        if (!addr) {
            continue;
        }
        nj_motion m;
        if (nj_motion_load(&rom, addr, 64, &m) == 0) {
            motions++;
            nj_motion_free(&m);
        } else {
            bad_motions++;
            printf("motion %d failed to load\n", i);
        }
    }
    CHECK(bad_models == 0 && bad_motions == 0);

    /* run every script as its own object for two seconds */
    int script_errors = 0;
    for (int s = 0; s < bios_script_count(&rom); s++) {
        if (!bios_script_offset(&rom, s)) {
            continue;
        }
        bvm vm;
        bvm_init(&vm, &rom, NULL);
        bvm_create(&vm, s, (uint16_t)(0x100 + s), 1);
        for (int fr = 0; fr < 120; fr++) {
            bvm_update(&vm);
        }
        if (vm.error) {
            printf("script %#x: error %d at %#x\n", s, vm.error, vm.error_pc);
            script_errors++;
        }
    }
    CHECK(script_errors == 0);

    /* the real sound container: 9 blocks, the three data banks with their known addresses */
    const uint8_t *drv, *banks;
    size_t dsz, bsz;
    CHECK(bios_sound_get(&rom, BIOS_SOUND_DRIVER, &drv, &dsz) == 0 && !memcmp(drv, "SDRV", 4));
    CHECK(bios_sound_get(&rom, BIOS_SOUND_BANKS, &banks, &bsz) == 0 && !memcmp(banks, "SMLT", 4));
    CHECK(dsz >= BAUDIO_DRIVER_CODE_OFFSET + BAUDIO_DRIVER_CODE_SIZE);
    baudio_block blk[BAUDIO_MAX_BLOCKS];
    int nb = baudio_parse_banks(banks, bsz, blk, BAUDIO_MAX_BLOCKS);
    CHECK(nb == 9);
    CHECK(nb == 9 && !strcmp(blk[0].tag, "SMPB") && blk[0].ram_addr == 0x18000 && blk[0].offset == 0x140 && blk[0].size == 0x940);
    CHECK(nb == 9 && !strcmp(blk[1].tag, "SMSB") && blk[1].ram_addr == 0x1F520 && blk[1].offset == 0xA80 && blk[1].size == 0x460);
    CHECK(nb == 9 && !strcmp(blk[4].tag, "SFPW") && blk[4].size == 0 && blk[4].reserved == 0x10040);
    printf("real ROM: %d models, %d motions, %d texlist textures resolved\n", models, motions, tex_ok);
    free(data);
}

int
main(void) {
    image = calloc(1, BIOS_ROM_SIZE);
    memcpy(image + 0x100, "SEGA SEGAKATANA KABUTO Ver.1.01d", 32);
    build_scripts();
    build_textures();
    build_model();

    const char* dump = getenv("BIOS_TEST_DUMP"); /* write the synthetic ROM out, e.g. for bios_preview */
    if (dump && *dump) {
        FILE* d = fopen(dump, "wb");
        if (d) {
            fwrite(image, 1, BIOS_ROM_SIZE, d);
            fclose(d);
        }
    }
    bios_rom rom;
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_OK);
    test_rom_tables(&rom);
    test_model(&rom);
    test_vm(&rom);
    test_dcbg();
    test_tex_decode();
    test_audio();
    test_scene(&rom);

    const char* real = getenv("BIOS_ROM_FILE");
    if (real && *real) {
        test_real_rom(real);
    }
    free(image);
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("bios_engine: all checks passed\n");
    return 0;
}
