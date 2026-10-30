/* Host test of iblue_gameid (the block openMenu sends to an iBlueControlMod). See test/run_iblue_gameid.sh. */
#include <stdio.h>
#include <string.h>

#include "iblue_gameid.h"

static int fails, checks;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                \
        }                                                                                                              \
    } while (0)

/* An adapter as the firmware behaves: a complete, valid block is kept (and applied); a bad one resets it */
static struct { int present; uint8_t box[512]; int drop_writes; int writes, reads; } fake;

static void reset_box(void) {
    memset(fake.box, 0, 512);
    iblue_gameid_block(fake.box, 0, "");
    fake.box[8] = fake.box[9] = fake.box[10] = fake.box[11] = 0;
}
static int fake_read(void* c, int block, uint8_t* buf) {
    (void)c;
    fake.reads++;
    if (block != IBLUE_GAMEID_BLOCK) return -1;
    if (!fake.present) { memset(buf, 0, 512); return 0; }
    memcpy(buf, fake.box, 512);
    return 0;
}
static int fake_write(void* c, int block, const uint8_t* buf) {
    (void)c;
    fake.writes++;
    if (block != IBLUE_GAMEID_BLOCK) return -1;
    if (fake.drop_writes > 0) { fake.drop_writes--; reset_box(); return 0; } /* a lost frame: the adapter drops the block */
    if (iblue_gameid_present(buf)) memcpy(fake.box, buf, 512);
    return 0;
}
static const iblue_gameid_io io = {NULL, fake_read, fake_write};

int main(void) {
    uint8_t b[512];

    /* the layout, as a fixed vector (the adapter's own test builds the same bytes) */
    iblue_gameid_block(b, 0x01020304u, "MK51035");
    CHECK(memcmp(b, "iBGI", 4) == 0 && b[4] == 1 && b[8] == 0x04 && b[11] == 0x01 && memcmp(b + 12, "MK51035", 7) == 0 && b[19] == 0);
    CHECK(b[28] == (uint8_t)(0x8593120f) && b[29] == (uint8_t)((0x8593120f) >> 8) && b[30] == (uint8_t)((0x8593120f) >> 16) && b[31] == (uint8_t)((0x8593120f) >> 24));
    CHECK(iblue_gameid_present(b));
    b[13] ^= 1;
    CHECK(!iblue_gameid_present(b));

    /* ids are made safe: odd characters become _, trailing blanks go, long ids are cut at 16 */
    iblue_gameid_block(b, 1, "T-8101N 50  ");
    CHECK(memcmp(b + 12, "T-8101N_50", 10) == 0 && b[22] == 0);
    iblue_gameid_block(b, 1, "../etc");
    CHECK(memcmp(b + 12, "___etc", 6) == 0 && b[18] == 0);
    iblue_gameid_block(b, 1, "ABCDEFGHIJKLMNOPQRSTUV");
    CHECK(memcmp(b + 12, "ABCDEFGHIJKLMNOP", 16) == 0);
    iblue_gameid_block(b, 1, "");
    CHECK(b[12] == 0 && iblue_gameid_present(b));

    /* an ordinary memory card: the block is not ours, nothing is written */
    memset(&fake, 0, sizeof(fake));
    CHECK(iblue_gameid_send(&io, 1, "MK51035") == -1 && fake.writes == 0);

    /* an adapter with the feature */
    memset(&fake, 0, sizeof(fake));
    fake.present = 1;
    reset_box();
    CHECK(iblue_gameid_send(&io, 5, "MK51035") == 0 && fake.writes == 1);
    CHECK(memcmp(fake.box + 12, "MK51035", 7) == 0 && fake.box[8] == 5);
    CHECK(iblue_gameid_send(&io, 6, "") == 0 && fake.box[12] == 0 && fake.box[8] == 6);       /* no game */
    CHECK(iblue_gameid_send(&io, 0, "ABC") == 0 && fake.box[8] == 1);                          /* seq 0 is avoided */

    /* a lost frame is retried (twice is fine, three times is not) */
    fake.writes = 0; fake.drop_writes = 2;
    CHECK(iblue_gameid_send(&io, 7, "MK51035") == 0 && fake.writes == 3);
    fake.drop_writes = 3;
    CHECK(iblue_gameid_send(&io, 8, "MK51035") == -2);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
