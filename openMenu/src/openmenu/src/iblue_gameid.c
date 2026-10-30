/* iblue_gameid: see iblue_gameid.h. */
#include <string.h>

#include "iblue_gameid.h"

#define ID_MAX 16

static uint32_t
crc32(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFFu;
    while (len-- > 0) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void
put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void
iblue_gameid_block(uint8_t out[IBLUE_GAMEID_BLOCK_SIZE], uint32_t seq, const char* product) {
    int n = 0, len = (int)strlen(product);
    while (len > 0 && product[len - 1] == ' ') {
        len--; /* trailing padding of the product number */
    }
    memset(out, 0, IBLUE_GAMEID_BLOCK_SIZE);
    memcpy(out, "iBGI", 4);
    out[4] = 1; /* version */
    put32(out + 8, seq);
    for (int i = 0; i < len && n < ID_MAX; i++, n++) {
        char c = product[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_';
        out[12 + n] = (uint8_t)(ok ? c : '_');
    }
    put32(out + 28, crc32(out, 28));
}

int
iblue_gameid_present(const uint8_t block[IBLUE_GAMEID_BLOCK_SIZE]) {
    uint32_t crc = (uint32_t)block[28] | ((uint32_t)block[29] << 8) | ((uint32_t)block[30] << 16) | ((uint32_t)block[31] << 24);
    return memcmp(block, "iBGI", 4) == 0 && block[4] == 1 && crc32(block, 28) == crc;
}

int
iblue_gameid_send(const iblue_gameid_io* io, uint32_t seq, const char* product) {
    uint8_t want[IBLUE_GAMEID_BLOCK_SIZE], got[IBLUE_GAMEID_BLOCK_SIZE];

    if (seq == 0) {
        seq = 1; /* 0 means "nothing applied yet" on the adapter */
    }
    if (io->read(io->ctx, IBLUE_GAMEID_BLOCK, got) != 0 || !iblue_gameid_present(got)) {
        return -1;
    }
    iblue_gameid_block(want, seq, product);
    for (int tries = 0; tries < 3; tries++) {
        if (io->write(io->ctx, IBLUE_GAMEID_BLOCK, want) == 0 && io->read(io->ctx, IBLUE_GAMEID_BLOCK, got) == 0 &&
            memcmp(got, want, 32) == 0) {
            return 0; /* what the adapter shows is what was sent */
        }
    }
    return -2;
}
