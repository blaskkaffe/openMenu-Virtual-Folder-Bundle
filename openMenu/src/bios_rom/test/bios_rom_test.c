/*
 * Host test for bios_rom. Builds a synthetic 2 MiB ROM image that mimics the
 * 1.01d structures (no Sega data involved) and checks every accessor.
 *
 * If BIOS_ROM_FILE points at a real dump, the same module is also run against
 * it and the counts documented for 1.01d are checked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios_rom.h"

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void
put16(uint8_t* rom, uint32_t o, uint16_t v) {
    rom[o] = (uint8_t)v;
    rom[o + 1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t* rom, uint32_t o, uint32_t v) {
    put16(rom, o, (uint16_t)v);
    put16(rom, o + 2, (uint16_t)(v >> 16));
}

/* Textures as found in 1.01d: ROM offset, GBIX, width, height */
static const struct {
    uint32_t off;
    uint32_t gbix;
    uint16_t w, h;
} tex_table[] = {
    {0x0728B0, 18, 32, 32},   {0x0730D0, 17, 32, 32},  {0x0738F0, 32, 64, 64},   {0x075910, 12, 64, 64},
    {0x077930, 0, 256, 256},  {0x07C150, 11, 32, 32},  {0x07C970, 114, 128, 32}, {0x07E990, 2, 32, 32},
    {0x07F1B0, 3, 32, 32},    {0x07F9D0, 5, 32, 32},   {0x0801F0, 6, 32, 32},    {0x080A10, 7, 64, 64},
    {0x082A30, 8, 32, 32},    {0x083250, 9, 128, 16},  {0x084270, 10, 128, 16},  {0x085290, 1, 8, 8},
    {0x085330, 0, 256, 256},  {0x089B50, 114, 128, 32},
};
#define NUM_TEX ((int)(sizeof(tex_table) / sizeof(tex_table[0])))

static uint8_t
type_for(uint16_t w, uint16_t h) {
    if (w == 256) {
        return BIOS_PVR_VQ;
    }
    return (w == h) ? BIOS_PVR_TWIDDLED : BIOS_PVR_RECTANGLE;
}

static uint8_t*
build_rom(const char* revision) {
    uint8_t* rom = calloc(1, BIOS_ROM_SIZE);
    char header[64];
    snprintf(header, sizeof(header), "SEGA SEGAKATANA KABUTO Ver.%s", revision);
    memcpy(rom + 0x100, header, strlen(header));

    /* script bank: 88 relative offsets, script i at bank + 0x160 + 16 * i; one unused slot */
    const uint32_t bank = 0x6F5BC;
    for (int i = 0; i < 88; i++) {
        put32(rom, bank + 4u * i, 0x160 + 16u * (uint32_t)i - 4u * (uint32_t)i);
    }
    put32(rom, bank + 4 * 0x46, 0xFFFFFEE8u); /* unused: points before the bank */

    for (int i = 0; i < NUM_TEX; i++) {
        uint32_t o = tex_table[i].off;
        uint16_t w = tex_table[i].w, h = tex_table[i].h;
        memcpy(rom + o, "GBIX", 4);
        put32(rom, o + 4, 8);
        put32(rom, o + 8, tex_table[i].gbix);
        memcpy(rom + o + 16, "PVRT", 4);
        rom[o + 16 + 8] = BIOS_PVR_RGB565;
        rom[o + 16 + 9] = type_for(w, h);
        put16(rom, o + 16 + 12, w);
        put16(rom, o + 16 + 14, h);
        /* a fake "GBIX" inside the payload must not be reported as a texture */
        memcpy(rom + o + 32 + 64, "GBIX", 4);
        rom[o + 32] = (uint8_t)i; /* first payload byte identifies the texture */
    }

    /* EN messages: id 0x0001 -> "Game", id 0x0002 -> "Files"; JP table empty */
    const uint32_t en = 0x39BA4;
    memcpy(rom + 0x39000, "Game", 5);
    memcpy(rom + 0x39010, "Files", 6);
    put16(rom, en, 1);
    put32(rom, en + 4, 0x8C039000);
    put16(rom, en + 20, 2);
    put32(rom, en + 20 + 4, 0x8C039010);
    put16(rom, en + 40, 0xFFFF);
    put16(rom, 0x39460, 0xFFFF);
    return rom;
}

static void
test_good_rom(void) {
    uint8_t* image = build_rom("1.01d");
    bios_rom rom;
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_OK);
    CHECK(!strcmp(rom.revision, "1.01d"));

    /* textures */
    CHECK(bios_texture_count(&rom) == NUM_TEX);
    for (int i = 0; i < NUM_TEX; i++) {
        bios_texture t;
        CHECK(bios_texture_get(&rom, i, &t) == 0);
        CHECK(t.gbix == tex_table[i].gbix);
        CHECK(t.width == tex_table[i].w && t.height == tex_table[i].h);
        CHECK(t.offset == tex_table[i].off);
        CHECK(t.data_size == bios_texture_payload_size(t.data_type, t.width, t.height));
        CHECK(t.data[0] == (uint8_t)i);
        CHECK(t.pixel_format == BIOS_PVR_RGB565);
    }
    bios_texture t;
    CHECK(bios_texture_get(&rom, NUM_TEX, &t) != 0);
    CHECK(bios_texture_get(&rom, -1, &t) != 0);
    CHECK(bios_texture_find(&rom, 114, &t) == 0 && t.offset == 0x07C970); /* first of the duplicates */
    CHECK(bios_texture_find(&rom, 666, &t) != 0);

    /* the real layout: header (0x20) + payload leaves no gap to the next texture */
    CHECK(tex_table[0].off + 0x20 + bios_texture_payload_size(BIOS_PVR_TWIDDLED, 32, 32) == tex_table[1].off);
    CHECK(tex_table[4].off + 0x20 + bios_texture_payload_size(BIOS_PVR_VQ, 256, 256) == tex_table[5].off);
    CHECK(bios_texture_payload_size(BIOS_PVR_TWIDDLED_MIPMAP, 32, 32) == 0);

    /* scripts */
    CHECK(bios_script_count(&rom) == 88);
    CHECK(bios_script_offset(&rom, 0) == 0x6F5BC + 0x160);
    CHECK(bios_script_offset(&rom, 5) == 0x6F5BC + 0x160 + 16 * 5);
    CHECK(bios_script_offset(&rom, 0x46) == 0);
    CHECK(bios_script_offset(&rom, 88) == 0);
    CHECK(bios_script_offset(&rom, -1) == 0);

    /* messages */
    CHECK(!strcmp(bios_message(&rom, BIOS_LANG_EN, 1, 0), "Game"));
    CHECK(!strcmp(bios_message(&rom, BIOS_LANG_EN, 2, 0), "Files"));
    CHECK(bios_message(&rom, BIOS_LANG_EN, 3, 0) == NULL);
    CHECK(bios_message(&rom, BIOS_LANG_EN, 1, 1) == NULL); /* line pointer 0 = not in ROM */
    CHECK(bios_message(&rom, BIOS_LANG_JP, 1, 0) == NULL);
    CHECK(bios_message(&rom, BIOS_LANG_EN, 1, 4) == NULL);

    /* sound */
    const uint8_t* d;
    size_t n;
    CHECK(bios_sound_get(&rom, BIOS_SOUND_DRIVER, &d, &n) == 0 && d == image + 0x1A0040 && n == 0x7A00);
    CHECK(bios_sound_get(&rom, BIOS_SOUND_JINGLE_R, &d, &n) == 0 && d == image + 0x1D4740 && n == 0x2B175);
    CHECK(bios_sound_get(&rom, (bios_sound_entry)4, &d, &n) != 0);

    /* raw access */
    CHECK(bios_rom_ptr(&rom, 0x8C000000, 1) == image);
    CHECK(bios_rom_ptr(&rom, 0x8C1FFFFF, 1) == image + 0x1FFFFF);
    CHECK(bios_rom_ptr(&rom, 0x8C1FFFFF, 2) == NULL);
    CHECK(bios_rom_ptr(&rom, 0x8B000000, 1) == NULL);
    CHECK(bios_rom_u32(&rom, 0x1FFFFE) == 0);

    free(image);
}

static void
test_rejections(void) {
    bios_rom rom;
    /* the version string is informational: another revision with the same layout is fine */
    uint8_t* image = build_rom("1.022");
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_OK && !strcmp(rom.revision, "1.022"));
    free(image);

    /* a ROM with the tag but a different layout (as 1.004 has) is refused */
    image = build_rom("1.004");
    put32(image, 0x6F5BC, 0);
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_ERR_LAYOUT);
    CHECK(bios_texture_count(&rom) == 0); /* a failed init leaves a rom nothing can read from */
    free(image);

    /* right bank, but the textures are missing */
    image = build_rom("1.01d");
    memset(image + 0x70000, 0, 0x20000);
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_ERR_LAYOUT);
    free(image);

    image = build_rom("1.01d");
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE - 1) == BIOS_ROM_ERR_SIZE);
    CHECK(bios_rom_init(NULL, image, BIOS_ROM_SIZE) == BIOS_ROM_ERR_ARGS);
    CHECK(bios_rom_init(&rom, NULL, BIOS_ROM_SIZE) == BIOS_ROM_ERR_ARGS);

    put32(image, 0x6F5BC, 0x100); /* right string, wrong structure */
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_ERR_LAYOUT);
    CHECK(bios_script_count(&rom) == 0);
    free(image);

    /* an image without any boot ROM string */
    image = calloc(1, BIOS_ROM_SIZE);
    CHECK(bios_rom_init(&rom, image, BIOS_ROM_SIZE) == BIOS_ROM_ERR_REVISION);
    free(image);
}

static void
test_real_rom(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("FAIL cannot open %s\n", path);
        failures++;
        return;
    }
    uint8_t* image = malloc(BIOS_ROM_SIZE);
    size_t got = fread(image, 1, BIOS_ROM_SIZE, f);
    fclose(f);

    bios_rom rom;
    CHECK(got == BIOS_ROM_SIZE);
    CHECK(bios_rom_init(&rom, image, got) == BIOS_ROM_OK);
    CHECK(bios_texture_count(&rom) == NUM_TEX);
    for (int i = 0; i < NUM_TEX; i++) {
        bios_texture t;
        CHECK(bios_texture_get(&rom, i, &t) == 0 && t.offset == tex_table[i].off && t.gbix == tex_table[i].gbix
              && t.width == tex_table[i].w && t.height == tex_table[i].h && t.data_size != 0);
    }
    CHECK(bios_script_count(&rom) == 88);
    CHECK(bios_script_offset(&rom, 0) == 0x6F71C); /* script 0x0 @ 8c06f71c per menu_scripts.txt */
    CHECK(bios_script_offset(&rom, 5) == 0x6F73D);
    CHECK(bios_script_offset(&rom, 0x46) == 0);
    printf("real ROM: revision %s, %d textures, %d scripts\n", rom.revision, bios_texture_count(&rom),
           bios_script_count(&rom));
    free(image);
}

int
main(void) {
    test_good_rom();
    test_rejections();
    const char* real = getenv("BIOS_ROM_FILE");
    if (real && *real) {
        test_real_rom(real);
    }
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("bios_rom: all checks passed\n");
    return 0;
}
