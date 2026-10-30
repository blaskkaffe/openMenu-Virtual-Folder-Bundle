/*
 * Host test of the iBlue settings: link, remap rules and the Controllers screen, against the
 * firmware's command handler (test/fw) with an in-memory board. See test/run_iblue.sh.
 */
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "iblue_fake_board.h"
#include "iblue_map.h"
#include "ui_iblue.h"

static int fails, checks;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                \
        }                                                                                                              \
    } while (0)

/* gfx stubs: the screen's text is collected so the test can look for it */
static char drawn[8192];
void gfx_text(const char* s, float x, float y, float z, uint32_t argb, int shadow) {
    (void)x; (void)y; (void)z; (void)argb; (void)shadow;
    strncat(drawn, s, sizeof(drawn) - strlen(drawn) - 2);
    strcat(drawn, "\n");
}
void gfx_rrect(float x, float y, float w, float h, float r, float z, uint32_t argb) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)z; (void)argb; }
void gfx_rect(float x, float y, float w, float h, float z, uint32_t argb) { (void)x; (void)y; (void)w; (void)h; (void)z; (void)argb; }

static int shows(const char* text) {
    drawn[0] = '\0';
    uib_draw();
    return strstr(drawn, text) != NULL;
}

static const iblue_io io = {NULL, board_read, board_write, board_sleep};

static void press(button_t b) { uib_button(b); uib_tick(); }

static void test_constants(void) {
    const int cmds[] = {IBLUE_CMD_HELLO, IBLUE_CMD_PORT_INFO, IBLUE_CMD_GET_MAP, IBLUE_CMD_SET_MAP, IBLUE_CMD_SET_TYPE,
                        IBLUE_CMD_RESET_MAP, IBLUE_CMD_COMMIT, IBLUE_CMD_REVERT, IBLUE_CMD_LIVE};
    const int res[] = {IBLUE_RES_OK, IBLUE_RES_BAD_CMD, IBLUE_RES_BAD_ARG, IBLUE_RES_BAD_CRC, IBLUE_RES_LOCKOUT, IBLUE_RES_FAILED};
    CHECK(fw_constants_match(cmds, 9, res, 6));
}

static void test_link(void) {
    iblue_link l;
    iblue_port_info pi;
    iblue_map_entry m[IBLUE_MAP_MAX];
    int size = 0, connected = 0;
    uint32_t w[4];

    board_reset();
    board_set_present(0);
    CHECK(iblue_link_open(&l, &io) == IBLUE_ERR_NO_BOARD);
    board_reset();
    CHECK(iblue_link_open(&l, &io) == 0 && l.map_max == 128);
    CHECK(board_stat(2) > 0); /* it had to wait for the board's task */
    CHECK(iblue_link_port_info(&l, 1, &pi) == 0 && pi.connected && strcmp(pi.name, "Fake Pad") == 0 && pi.map_size == 118);
    CHECK(iblue_link_port_info(&l, 0, &pi) == 0 && !pi.connected && pi.acc_mode == 1);
    CHECK(iblue_link_port_info(&l, 7, &pi) == IBLUE_RES_BAD_ARG);
    CHECK(iblue_link_get_map(&l, 2, m, &size) == 0 && size == 118);
    CHECK(m[18].src_btn == 18 && m[18].dst_btn == 18 && m[18].dst_id == 2 && m[117].dst_btn == 127);

    /* a table update that spans several blocks (all 118 entries) */
    for (int i = 0; i < size; i++) m[i].perc_deadzone = 99;
    CHECK(iblue_link_set_map(&l, 3, 0, 100, m, -1) == 0 && l.unsaved);
    CHECK(iblue_link_set_map(&l, 3, 100, 18, m + 100, -1) == 0);
    {
        iblue_map_entry back[IBLUE_MAP_MAX];
        CHECK(iblue_link_get_map(&l, 3, back, &size) == 0 && size == 118 && back[117].perc_deadzone == 99 &&
              back[3].perc_deadzone == 99);
    }
    CHECK(iblue_link_set_type(&l, 0, 0, 2) == IBLUE_RES_LOCKOUT && board_acc(0) == 1);
    CHECK(iblue_link_set_type(&l, 2, 1, 2) == 0 && board_dev(2) == 1 && board_acc(2) == 2);
    CHECK(iblue_link_revert(&l) == 0 && !l.unsaved && board_dev(2) == 0);

    board_set_held(1, 1u << 19);
    CHECK(iblue_link_live(&l, 1, w, &connected) == 0 && connected == 1 && w[0] == (1u << 19));
    CHECK(iblue_link_live(&l, 0, w, &connected) == 0 && connected == 0);
}

static void test_assign(void) {
    iblue_map_entry m[IBLUE_MAP_MAX];
    int size = 118, first, count, ids[4];

    for (int j = 0; j < size; j++) m[j] = (iblue_map_entry){(uint8_t)j, (uint8_t)j, 0, 100, 50, 135, 0, 0};
    CHECK(iblue_output_index(18) == 0 && strcmp(iblue_output_name(0), "A") == 0 && iblue_output_index(99) == -1);
    for (int i = 0; i < IBLUE_OUTPUTS; i++) CHECK(iblue_output_index(iblue_output_id(i)) == i);

    /* source 19 (face up) becomes A: A's old source (18) takes 19's old output (the swap) */
    CHECK(iblue_assign(m, &size, 0, 19, 0, &first, &count) == 0);
    CHECK(first == 18 && count == 2 && m[19].dst_btn == 18 && m[18].dst_btn == 19);
    CHECK(iblue_sources_of(m, size, 0, 0, ids, 4) == 1 && ids[0] == 19);
    /* assigning what is already there changes nothing */
    CHECK(iblue_assign(m, &size, 0, 19, 0, &first, &count) == 0 && count == 0);
    /* clear: nothing acts as A any more */
    iblue_clear_output(m, size, 0, 0, &first, &count);
    CHECK(count == 1 && first == 19 && m[19].dst_btn == IBLUE_DST_NONE && iblue_sources_of(m, size, 0, 0, ids, 4) == 0);
    /* a source that was unassigned takes an output and the old holders become unassigned */
    CHECK(iblue_assign(m, &size, 0, 19, 3, &first, &count) == 0);
    CHECK(m[19].dst_btn == 19 && m[19].src_btn == 19);
    CHECK(iblue_assign(m, &size, 0, 5, 1, &first, &count) == 0 && m[5].dst_btn == 17);
    /* entries of other ports are left alone */
    m[40].dst_id = 1; m[40].dst_btn = 25;
    CHECK(iblue_assign(m, &size, 0, 5, 6, &first, &count) == 0 && m[40].dst_btn == 25);
    /* a source with no entry gets one, until the table is full */
    int old = size;
    CHECK(iblue_assign(m, &size, 0, 200, 4, &first, &count) == 0 && size == old + 1 && m[old].src_btn == 200 && m[old].dst_btn == 20);
    size = IBLUE_MAP_MAX;
    CHECK(iblue_assign(m, &size, 0, 201, 4, &first, &count) == -1);
    CHECK(strcmp(iblue_source_name(24, NULL, 0), "L2 trigger") == 0);
}

static void test_ui(void) {
    board_reset();
    uib_open(&io);
    CHECK(!uib_done() && shows("Controllers") && shows("Remap buttons") && shows("no controller connected"));

    /* port A, no controller: Accessories cycles but keeps the VMU (port A's channel) */
    press(BTN_DOWN); press(BTN_DOWN);
    press(BTN_RIGHT);
    CHECK(board_acc(0) == 3);
    press(BTN_RIGHT);
    CHECK(board_acc(0) == 1);
    press(BTN_UP); press(BTN_RIGHT); /* Type: controller (alt) */
    CHECK(board_dev(0) == 1 && shows("Controller (alt)"));
    press(BTN_LEFT);
    CHECK(board_dev(0) == 0);

    /* Remap on port A without a controller: A opens the list of buttons */
    press(BTN_DOWN); press(BTN_DOWN); press(BTN_A);
    CHECK(shows("Start") && shows("D button"));  /* the remap list */
    press(BTN_A);
    CHECK(shows("Button for A") && shows("Face down"));
    for (int i = 0; i < 15; i++) press(BTN_UP); /* the list opens on the current button (face down, 18) */
    press(BTN_A); /* source 3 (left stick up) becomes the A button */
    CHECK(board_dst(0, 3) == 18 && board_dst(0, 18) == 3);
    CHECK(shows("L stick up"));
    press(BTN_Y); /* clear A */
    CHECK(board_dst(0, 3) == IBLUE_DST_NONE && shows("-"));
    press(BTN_B);

    /* port B has a controller: press-to-learn */
    press(BTN_UP); press(BTN_UP); press(BTN_UP);
    press(BTN_RIGHT);
    CHECK(shows("Fake Pad"));
    press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN); press(BTN_A);
    press(BTN_DOWN);            /* output "B" */
    board_set_held(1, 1u << 19); /* held while the screen opens: ignored until released */
    press(BTN_A);
    CHECK(shows("Press the button for B"));
    for (int i = 0; i < 30; i++) uib_tick();
    CHECK(board_dst(1, 19) == 19);
    board_set_held(1, 0);
    for (int i = 0; i < 12; i++) uib_tick();
    CHECK(board_dst(1, 19) == 19); /* nothing changed yet */
    board_set_held(1, 1u << 25);   /* L1 */
    for (int i = 0; i < 12; i++) uib_tick();
    CHECK(board_dst(1, 25) == 17 && board_dst(1, 17) == 25); /* L1 is B, the old B source got Z */
    CHECK(shows("L1 bumper"));
    board_set_held(1, 0);
    press(BTN_B); /* ignored right after learning */
    CHECK(shows("Press a button") == 0 && shows("Port B   A: press a button"));
    for (int i = 0; i < 30; i++) uib_tick();

    /* learning gives up when nothing is pressed */
    press(BTN_A);
    CHECK(shows("Press the button for C") || shows("Press the button for B"));
    for (int i = 0; i < 700; i++) uib_tick();
    CHECK(!shows("Press the button for") && strcmp(uib_notice(), "Nothing pressed") == 0);
    for (int i = 0; i < 30; i++) uib_tick();
    press(BTN_B);
    CHECK(shows("Reset this controller"));

    /* leaving with unsaved changes asks; X discards */
    CHECK(shows("Save and exit") && shows("(unsaved changes)"));
    press(BTN_B);
    CHECK(shows("Unsaved changes"));
    press(BTN_B);
    CHECK(!uib_done() && !shows("Unsaved changes"));
    press(BTN_B);
    press(BTN_X);
    CHECK(uib_done() && board_stat(1) == 1 && board_dst(1, 25) == 25);

    /* save */
    board_reset();
    uib_open(&io);
    press(BTN_DOWN); press(BTN_DOWN); press(BTN_RIGHT);
    CHECK(board_acc(0) == 3);
    press(BTN_B);
    press(BTN_A);
    CHECK(uib_done() && board_stat(0) == 1 && strcmp(uib_notice(), "Saved") == 0);

    /* no board */
    board_reset();
    board_set_present(0);
    uib_open(&io);
    CHECK(shows("No iBlueControlMod found") && !uib_done());
    press(BTN_B);
    CHECK(uib_done());
}

int main(void) {
    test_constants();
    test_link();
    test_assign();
    test_ui();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
