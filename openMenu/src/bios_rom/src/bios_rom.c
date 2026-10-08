/*
 * bios_rom: read-only access to the assets in the Dreamcast boot ROM.
 * See bios_rom.h. Layout facts come from the boot ROM decompile notes
 * (revision 1.01d); nothing here copies ROM data.
 */
#include "bios_rom.h"

#include <string.h>

#define SUPPORTED_REVISION "1.01d"

/* Menu image layout (ROM offset == RAM address - 0x8C000000) */
#define SCRIPT_BANK_OFFSET 0x6F5BCu
#define SCRIPT_BANK_COUNT 88
#define TEXTURE_SCAN_START 0x70000u
#define TEXTURE_SCAN_END 0x90000u
#define HEADER_SCAN_LEN 0x10000u

#define MESSAGE_STRIDE 20u
#define MESSAGE_END_ID 0xFFFFu

/* Message tables per language, as RAM addresses (index = bios_lang) */
static const uint32_t message_tables[] = {0x8C039460u, 0x8C039BA4u, 0x8C03AA2Cu, 0x8C03A2E8u, 0x8C03B170u, 0x8C03B8B4u};

typedef struct {
    uint32_t offset;
    uint32_t size;
} sound_span;

/* ROM sound directory of 1.01d: driver, SMLT banks, jingle L, jingle R */
static const sound_span sound_dir[] = {
    {0x1A0040u, 0x79F4u},
    {0x1A7A40u, 0x1B80u},
    {0x1A95C0u, 0x2B175u},
    {0x1D4740u, 0x2B175u},
};

static const uint8_t*
find_bytes(const uint8_t* hay, size_t hay_len, const char* needle, size_t needle_len) {
    if (needle_len == 0 || hay_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (hay[i] == (uint8_t)needle[0] && !memcmp(hay + i, needle, needle_len)) {
            return hay + i;
        }
    }
    return NULL;
}

uint16_t
bios_rom_u16(const bios_rom* rom, uint32_t offset) {
    if (!rom || !rom->data || (size_t)offset + 2 > rom->size) {
        return 0;
    }
    return (uint16_t)(rom->data[offset] | (rom->data[offset + 1] << 8));
}

uint32_t
bios_rom_u32(const bios_rom* rom, uint32_t offset) {
    if (!rom || !rom->data || (size_t)offset + 4 > rom->size) {
        return 0;
    }
    return (uint32_t)rom->data[offset] | ((uint32_t)rom->data[offset + 1] << 8) | ((uint32_t)rom->data[offset + 2] << 16)
           | ((uint32_t)rom->data[offset + 3] << 24);
}

const uint8_t*
bios_rom_ptr(const bios_rom* rom, uint32_t addr, size_t len) {
    if (!rom || !rom->data || addr < BIOS_ROM_RAM_BASE) {
        return NULL;
    }
    uint32_t off = addr - BIOS_ROM_RAM_BASE;
    if ((size_t)off > rom->size || len > rom->size - off) {
        return NULL;
    }
    return rom->data + off;
}

int
bios_rom_init(bios_rom* rom, const void* data, size_t size) {
    if (!rom || !data) {
        return BIOS_ROM_ERR_ARGS;
    }
    memset(rom, 0, sizeof(*rom));
    if (size < BIOS_ROM_SIZE) {
        return BIOS_ROM_ERR_SIZE;
    }

    const uint8_t* bytes = (const uint8_t*)data;
    static const char tag[] = "KABUTO Ver.";
    const uint8_t* p = find_bytes(bytes, HEADER_SCAN_LEN, tag, sizeof(tag) - 1);
    if (!p) {
        return BIOS_ROM_ERR_REVISION;
    }

    p += sizeof(tag) - 1;
    size_t n = 0;
    while (n < sizeof(rom->revision) - 1 && ((p[n] >= '0' && p[n] <= '9') || (p[n] >= 'a' && p[n] <= 'z') || p[n] == '.')) {
        rom->revision[n] = (char)p[n];
        n++;
    }
    rom->revision[n] = '\0';
    if (strcmp(rom->revision, SUPPORTED_REVISION) != 0) {
        return BIOS_ROM_ERR_REVISION;
    }

    rom->data = bytes;
    rom->size = BIOS_ROM_SIZE;

    /* The script bank starts with the offset of its first script, which is the
     * size of the offset table itself: one 32-bit entry per script. */
    if (bios_rom_u32(rom, SCRIPT_BANK_OFFSET) != SCRIPT_BANK_COUNT * 4u) {
        rom->data = NULL;
        rom->size = 0;
        return BIOS_ROM_ERR_LAYOUT;
    }
    return BIOS_ROM_OK;
}

/* ---- Textures ---------------------------------------------------------- */

size_t
bios_texture_payload_size(uint8_t data_type, uint16_t width, uint16_t height) {
    switch (data_type) {
        case BIOS_PVR_TWIDDLED:
        case BIOS_PVR_RECTANGLE: return (size_t)width * height * 2;
        case BIOS_PVR_VQ: return 2048 + (size_t)(width / 2) * (height / 2);
        default: return 0; /* mipmapped and other types are not used by the menu */
    }
}

/* Parse the texture whose GBIX header is at `off`. Returns 0 on success. */
static int
parse_texture(const bios_rom* rom, uint32_t off, bios_texture* out) {
    if ((size_t)off + 0x30 > rom->size || memcmp(rom->data + off, "GBIX", 4) != 0) {
        return -1;
    }
    uint32_t p = 0;
    for (uint32_t k = off + 8; k <= off + 0x20 && (size_t)k + 16 <= rom->size; k++) {
        if (!memcmp(rom->data + k, "PVRT", 4)) {
            p = k;
            break;
        }
    }
    if (!p) {
        return -1;
    }

    uint8_t fmt = rom->data[p + 8];
    uint8_t type = rom->data[p + 9];
    uint16_t w = bios_rom_u16(rom, p + 12);
    uint16_t h = bios_rom_u16(rom, p + 14);
    if (fmt > BIOS_PVR_ARGB4444 || w == 0 || h == 0 || w > 1024 || h > 1024) {
        return -1;
    }

    size_t size = bios_texture_payload_size(type, w, h);
    if ((size_t)p + 16 + size > rom->size) {
        return -1;
    }

    out->gbix = bios_rom_u32(rom, off + 8);
    out->pixel_format = fmt;
    out->data_type = type;
    out->width = w;
    out->height = h;
    out->data = rom->data + p + 16;
    out->data_size = size;
    out->offset = off;
    return 0;
}

/* Walk textures in ROM order. `visit` returns non-zero to stop. */
static int
scan_textures(const bios_rom* rom, int (*visit)(const bios_texture*, void*), void* ctx) {
    uint32_t end = TEXTURE_SCAN_END < rom->size ? TEXTURE_SCAN_END : (uint32_t)rom->size;
    for (uint32_t off = TEXTURE_SCAN_START; off + 4 <= end; off++) {
        bios_texture tex;
        if (rom->data[off] == 'G' && parse_texture(rom, off, &tex) == 0) {
            if (visit(&tex, ctx)) {
                return 1;
            }
            /* Skip the payload: it is arbitrary data that could contain "GBIX". */
            off = (uint32_t)(tex.data - rom->data) + (uint32_t)tex.data_size - 1;
        }
    }
    return 0;
}

static int
visit_count(const bios_texture* tex, void* ctx) {
    (void)tex;
    (*(int*)ctx)++;
    return 0;
}

typedef struct {
    int target;
    int seen;
    uint32_t gbix;
    int by_gbix;
    bios_texture* out;
} tex_query;

static int
visit_query(const bios_texture* tex, void* ctx) {
    tex_query* q = (tex_query*)ctx;
    if (q->by_gbix ? tex->gbix == q->gbix : q->seen == q->target) {
        *q->out = *tex;
        q->seen = -1; /* found */
        return 1;
    }
    q->seen++;
    return 0;
}

int
bios_texture_count(const bios_rom* rom) {
    int n = 0;
    if (rom && rom->data) {
        scan_textures(rom, visit_count, &n);
    }
    return n;
}

int
bios_texture_get(const bios_rom* rom, int n, bios_texture* out) {
    if (!rom || !rom->data || !out || n < 0) {
        return -1;
    }
    tex_query q = {n, 0, 0, 0, out};
    scan_textures(rom, visit_query, &q);
    return q.seen == -1 ? 0 : -1;
}

int
bios_texture_find(const bios_rom* rom, uint32_t gbix, bios_texture* out) {
    if (!rom || !rom->data || !out) {
        return -1;
    }
    tex_query q = {0, 0, gbix, 1, out};
    scan_textures(rom, visit_query, &q);
    return q.seen == -1 ? 0 : -1;
}

/* ---- Scripts ----------------------------------------------------------- */

int
bios_script_count(const bios_rom* rom) {
    return (rom && rom->data) ? (int)(bios_rom_u32(rom, SCRIPT_BANK_OFFSET) / 4) : 0;
}

uint32_t
bios_script_offset(const bios_rom* rom, int idx) {
    if (idx < 0 || idx >= bios_script_count(rom)) {
        return 0;
    }
    uint32_t entry = SCRIPT_BANK_OFFSET + (uint32_t)idx * 4u;
    uint32_t off = entry + bios_rom_u32(rom, entry); /* relative, wraps for negative */
    /* Unused slots point back at the bank or outside the ROM. */
    if (off <= SCRIPT_BANK_OFFSET || off >= rom->size) {
        return 0;
    }
    return off;
}

/* ---- Messages ---------------------------------------------------------- */

const char*
bios_message(const bios_rom* rom, bios_lang lang, uint16_t id, int line) {
    if (!rom || !rom->data || (unsigned)lang >= sizeof(message_tables) / sizeof(message_tables[0]) || line < 0 || line > 3) {
        return NULL;
    }

    uint32_t t = message_tables[lang] - BIOS_ROM_RAM_BASE;
    /* The table is a few hundred bytes; bound the walk so a bad ROM cannot loop. */
    for (int i = 0; i < 1024; i++, t += MESSAGE_STRIDE) {
        if ((size_t)t + MESSAGE_STRIDE > rom->size) {
            return NULL;
        }
        uint16_t mid = bios_rom_u16(rom, t);
        if (mid == MESSAGE_END_ID) {
            return NULL;
        }
        if (mid != id) {
            continue;
        }
        uint32_t addr = bios_rom_u32(rom, t + 4 + 4u * (uint32_t)line);
        if (addr < BIOS_ROM_RAM_BASE || addr - BIOS_ROM_RAM_BASE >= rom->size) {
            return NULL;
        }
        const char* s = (const char*)rom->data + (addr - BIOS_ROM_RAM_BASE);
        const void* nul = memchr(s, 0, rom->size - (addr - BIOS_ROM_RAM_BASE));
        return (nul && s[0]) ? s : NULL;
    }
    return NULL;
}

/* ---- Sound ------------------------------------------------------------- */

int
bios_sound_get(const bios_rom* rom, bios_sound_entry which, const uint8_t** data, size_t* size) {
    if (!rom || !rom->data || (unsigned)which >= sizeof(sound_dir) / sizeof(sound_dir[0])) {
        return -1;
    }
    const sound_span* s = &sound_dir[which];
    if ((size_t)s->offset + s->size > rom->size) {
        return -1;
    }
    if (data) {
        *data = rom->data + s->offset;
    }
    if (size) {
        *size = s->size;
    }
    return 0;
}
