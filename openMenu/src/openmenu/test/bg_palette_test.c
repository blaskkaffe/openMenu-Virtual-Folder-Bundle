/* Host test: gcc -Isrc test/bg_palette_test.c src/bg_palette.c */
#include <stdio.h>
#include <string.h>

#include "bg_palette.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int
main(void) {
    uint32_t t = 0xFFB0D0D0u, b = 0xFF4060C0u;
    bg_palette_apply(0, 0, &t, &b);
    CHECK(t == 0xFFB0D0D0u && b == 0xFF4060C0u); /* the BIOS gradient is untouched */
    bg_palette_apply(0, 1, &t, &b);
    CHECK(t == 0xFF424E4Eu && b == 0xFF182448u);
    t = 0xFFB0D0D0u; b = 0xFF4060C0u;
    bg_palette_apply(1, 0, &t, &b);
    CHECK(t == 0xFFB0D0B0u && b == 0xFF30A050u);
    t = 0xFFB0D0D0u; b = 0xFF4060C0u;
    bg_palette_apply(99, 0, &t, &b);
    CHECK(t == 0xFFB0D0D0u); /* out of range: ignored */
    CHECK(!strcmp(bg_palette_name(5), "Grey") && !strcmp(bg_palette_name(-1), "?"));
    for (int i = 0; i < BG_PALETTE_COUNT; i++) {
        t = 0xFFFFFFFFu; b = 0xFFFFFFFFu;
        bg_palette_apply(i, 1, &t, &b);
        CHECK((t >> 24) == 0xFF && (b & 0xFF) <= 0x60);
    }
    printf(failures ? "%d failures\n" : "bg_palette ok\n", failures);
    return failures != 0;
}
