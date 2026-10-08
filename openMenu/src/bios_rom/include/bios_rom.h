/*
 * bios_rom: read-only access to the assets in the Dreamcast boot ROM.
 *
 * Nothing from the ROM is shipped with the launcher. At run time the console's
 * own ROM (mapped at 0xA0000000 on the SH-4) is handed to bios_rom_init() and
 * everything is read from there in place. The module is plain C with no KOS
 * dependency so it can also be tested on a PC against a ROM dump.
 *
 * Offsets and layouts are those of boot ROM "KATANA KABUTO Ver.1.01d". The menu image is
 * byte-identical in 1.01c, 1.01d, 1.022 and 1.032, so bios_rom_init() accepts a ROM by its
 * structure, not by its version. Anything else (1.004, early dev ROMs) is rejected and the
 * caller is expected to fall back to something that does not need the ROM.
 */
#ifndef BIOS_ROM_H
#define BIOS_ROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIOS_ROM_SIZE 0x200000u    /* 2 MiB */
#define BIOS_ROM_RAM_BASE 0x8C000000u /* ROM offset X runs at RAM address BASE + X */

typedef enum bios_rom_status {
    BIOS_ROM_OK = 0,
    BIOS_ROM_ERR_ARGS = -1,
    BIOS_ROM_ERR_SIZE = -2,       /* smaller than 2 MiB */
    BIOS_ROM_ERR_REVISION = -3,   /* no boot ROM version tag in the image */
    BIOS_ROM_ERR_LAYOUT = -4      /* a boot ROM, but not with the menu image layout we know (e.g. 1.004) */
} bios_rom_status;

typedef struct bios_rom {
    const uint8_t* data;
    size_t size;
    char revision[8]; /* from the ROM header, e.g. "1.01d", NUL terminated; informational */
} bios_rom;

/* Validate `data` (a ROM image or the memory-mapped ROM) and fill `rom`. */
int bios_rom_init(bios_rom* rom, const void* data, size_t size);

/* Pointer to the byte at menu-image RAM address `addr` (0x8C000000 based).
 * NULL when [addr, addr + len) is not inside the ROM. */
const uint8_t* bios_rom_ptr(const bios_rom* rom, uint32_t addr, size_t len);

/* Little-endian reads at a ROM offset; 0 when out of range. */
uint16_t bios_rom_u16(const bios_rom* rom, uint32_t offset);
uint32_t bios_rom_u32(const bios_rom* rom, uint32_t offset);

/* ---- Textures (GBIX/PVRT blobs embedded in the menu image) -------------- */

/* PVRT pixel formats (byte 8 of the PVRT header) */
#define BIOS_PVR_ARGB1555 0
#define BIOS_PVR_RGB565 1
#define BIOS_PVR_ARGB4444 2

/* PVRT data types (byte 9 of the PVRT header) */
#define BIOS_PVR_TWIDDLED 1
#define BIOS_PVR_TWIDDLED_MIPMAP 2
#define BIOS_PVR_VQ 3
#define BIOS_PVR_RECTANGLE 9

typedef struct bios_texture {
    uint32_t gbix;          /* global index the menu scripts refer to */
    uint8_t pixel_format;   /* BIOS_PVR_ARGB1555 / RGB565 / ARGB4444 */
    uint8_t data_type;      /* BIOS_PVR_* */
    uint16_t width;
    uint16_t height;
    const uint8_t* data;    /* texture payload exactly as the PVR wants it */
    size_t data_size;       /* 0 when the data type is not supported */
    uint32_t offset;        /* ROM offset of the GBIX header */
} bios_texture;

/* Parse a .PVR file image (optional GBIX header, then PVRT) held in `buf`, e.g. a texture
 * read from the SD card. The result points into `buf`. Returns 0, -1 if it is not a
 * supported texture (mipmapped and palette formats are refused). */
int bios_texture_parse(const uint8_t* buf, size_t len, bios_texture* out);

/* Number of textures found in the menu image. */
int bios_texture_count(const bios_rom* rom);

/* Fill `out` with the n-th texture in ROM order. Returns 0, or -1 when n is out of range. */
int bios_texture_get(const bios_rom* rom, int n, bios_texture* out);

/* First texture with this GBIX (a few indices occur twice, use _get for those). */
int bios_texture_find(const bios_rom* rom, uint32_t gbix, bios_texture* out);

/* Payload size in bytes for a texture, 0 for unsupported data types. */
size_t bios_texture_payload_size(uint8_t data_type, uint16_t width, uint16_t height);

/* Texture whose GBIX header is at RAM address `addr` (as stored in texlists).
 * The disc-label buffer address used by the BIOS (0x8C341368) is mapped to the
 * default disc texture inside the ROM. */
int bios_texture_at(const bios_rom* rom, uint32_t addr, bios_texture* out);

/* ---- Models, motions and texture lists --------------------------------------
 * All three are tables of pointers (RAM addresses) in the menu image. */

#define BIOS_MODEL_COUNT 83
#define BIOS_MOTION_COUNT 20

/* RAM address of the Ninja object tree / motion / texlist, 0 when empty or out of range. */
uint32_t bios_model_addr(const bios_rom* rom, int idx);
uint32_t bios_motion_addr(const bios_rom* rom, int idx);
uint32_t bios_texlist_addr(const bios_rom* rom, int idx);

/* Number of textures in texlist `idx`, and the k-th one resolved to its ROM texture. */
int bios_texlist_count(const bios_rom* rom, int idx);
int bios_texlist_texture(const bios_rom* rom, int idx, int k, bios_texture* out);

/* ---- Menu scripts --------------------------------------------------------- */

/* Number of entries in the script bytecode bank (88 on 1.01d). */
int bios_script_count(const bios_rom* rom);

/* ROM offset of script `idx`, or 0 when the entry is unused / out of range. */
uint32_t bios_script_offset(const bios_rom* rom, int idx);

/* ---- Messages -------------------------------------------------------------- */

typedef enum bios_lang { BIOS_LANG_JP, BIOS_LANG_EN, BIOS_LANG_DE, BIOS_LANG_FR, BIOS_LANG_ES, BIOS_LANG_IT } bios_lang;

/* Line 0..3 of message `id` in `lang`, NUL terminated, borrowed from the ROM
 * (ISO-8859-1, Shift-JIS for BIOS_LANG_JP). NULL if missing or empty. */
const char* bios_message(const bios_rom* rom, bios_lang lang, uint16_t id, int line);

/* ---- Sound ------------------------------------------------------------------ */

typedef enum bios_sound_entry {
    BIOS_SOUND_DRIVER = 0, /* ARM7 driver "SDRV"; the ARM code starts 0x20 bytes in */
    BIOS_SOUND_BANKS = 1,  /* "SMLT" container: SMPB, SMSB, SFOB, SFPB */
    BIOS_SOUND_JINGLE_L = 2,
    BIOS_SOUND_JINGLE_R = 3
} bios_sound_entry;

int bios_sound_get(const bios_rom* rom, bios_sound_entry which, const uint8_t** data, size_t* size);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_ROM_H */
