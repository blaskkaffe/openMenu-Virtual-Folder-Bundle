/*
 * bios_audio: container parsing and command images, see bios_audio.h.
 */
#include "bios_audio.h"

#include <string.h>

#define RECORD_SIZE 32u
#define DIRECTORY_LIMIT 0x400u /* the directory is a few hundred bytes; never scan further */
#define SOUND_RAM_SIZE 0x200000u

static const char* const known_tags[] = {"SMPB", "SMSB", "SOSB", "SFOB", "SFPB", "SFPW", "SPSR"};

static uint32_t
rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The tag may be stored in either byte order; returns the canonical tag or NULL. */
static const char*
match_tag(const uint8_t* p) {
    for (unsigned i = 0; i < sizeof(known_tags) / sizeof(known_tags[0]); i++) {
        const char* t = known_tags[i];
        if (!memcmp(p, t, 4)) {
            return t;
        }
        if (p[0] == (uint8_t)t[3] && p[1] == (uint8_t)t[2] && p[2] == (uint8_t)t[1] && p[3] == (uint8_t)t[0]) {
            return t;
        }
    }
    return NULL;
}

int
baudio_parse_banks(const uint8_t* c, size_t size, baudio_block* out, int max) {
    int n = 0;
    if (!c || !out) {
        return 0;
    }
    for (size_t at = 0; at + RECORD_SIZE <= size && at < DIRECTORY_LIMIT && n < max; at += RECORD_SIZE) {
        const char* tag = match_tag(c + at);
        if (!tag) {
            continue; /* header record or padding */
        }
        baudio_block b;
        memset(&b, 0, sizeof(b));
        memcpy(b.tag, tag, 4);
        b.unit = rd32(c + at + 4);
        b.ram_addr = rd32(c + at + 8);
        b.reserved = rd32(c + at + 12);
        b.offset = rd32(c + at + 16);
        b.size = rd32(c + at + 20);
        if (b.ram_addr >= SOUND_RAM_SIZE || b.unit > 15) {
            continue;
        }
        if (b.size && ((size_t)b.offset + b.size > size)) {
            continue; /* data would lie outside the container: not the layout we think */
        }
        out[n++] = b;
    }
    return n;
}

uint32_t
baudio_datamap_addr(const baudio_block* b) {
    uint32_t base;
    if (!strcmp(b->tag, "SMSB")) base = 0x000;
    else if (!strcmp(b->tag, "SMPB")) base = 0x080;
    else if (!strcmp(b->tag, "SOSB")) base = 0x100;
    else if (!strcmp(b->tag, "SPSR")) base = 0x180;
    else if (!strcmp(b->tag, "SFPB")) return BAUDIO_DATA_MAP + 0x200;
    else if (!strcmp(b->tag, "SFOB")) return BAUDIO_DATA_MAP + 0x280;
    else if (!strcmp(b->tag, "SFPW")) return BAUDIO_DATA_MAP + 0x288;
    else return 0;
    return BAUDIO_DATA_MAP + base + 8u * b->unit;
}

void
baudio_datamap_entry(const baudio_block* b, uint32_t words[2]) {
    words[0] = b->ram_addr;
    words[1] = b->size ? b->size : b->reserved;
}

void
baudio_cmd_play(uint8_t slot[16], int player, int bank_unit, int sequence, int priority) {
    memset(slot, 0, 16);
    slot[0] = BAUDIO_CMD_PLAY;
    slot[2] = (uint8_t)player;
    slot[3] = (uint8_t)bank_unit;
    slot[4] = (uint8_t)sequence;
    slot[5] = (uint8_t)(priority << 3);
}

void
baudio_cmd_master_volume(uint8_t slot[16], int volume) {
    memset(slot, 0, 16);
    slot[0] = BAUDIO_CMD_MASTER_VOLUME;
    slot[2] = (uint8_t)((volume & 0xF) << 4);
}

void
baudio_cmd_stereo(uint8_t slot[16], int mono) {
    memset(slot, 0, 16);
    slot[0] = BAUDIO_CMD_STEREO_MONO;
    slot[2] = mono ? 0xFF : 0x00;
}
