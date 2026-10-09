/* ui_files: see ui_files.h. Messages are those of the BIOS file screen (messages_en). */
#include <stdio.h>
#include <string.h>

#include <bios_audio.h>
#include <bios_files.h>

#include "gfx.h"
#include "sound.h"
#include "ui_files.h"
#include "vmu_files.h"

typedef enum {
    FS_GRID,         /* pick a card */
    FS_BROWSE,       /* the files of the card */
    FS_FILEMENU,     /* Copy / Delete / Cancel */
    FS_DEST,         /* pick the card to copy to */
    FS_ASK_OVERWRITE,
    FS_ASK_DELETE,
    FS_BUSY,         /* a copy, delete or reset is about to run */
    FS_MESSAGE,
    FS_CARDMENU,     /* Change icon / Change colour / Memory reset / Cancel */
    FS_ICONPICK,
    FS_COLOURPICK,
    FS_ASK_RESET,    /* "all files will be deleted, proceed?" */
    FS_ASK_CONFIRM   /* "confirm your settings" before the reset */
} fstate;

static bmenu* menu;
static bfiles grid;
static fstate state;
static fstate after_message;
static int src_slot, dst_slot;
static vf_file files[VF_MAX_FILES];
static int nfiles, file_cursor, file_top;
static int free_blocks[BFILES_SLOTS];
static int popup_sel;
static int busy_op, busy_frames; /* 0 copy, 1 delete, 2 look change, 3 memory reset */
static int look_shape, look_colour; /* the icon and colour being chosen */
static int picking_for_reset;      /* the icon / colour pickers are part of the memory reset */
static int icon_cursor, icon_page_base = -1;
static int icon_tex_page[32];
static int colour_cursor;
static int file_icon[VF_MAX_FILES]; /* picture of each file: -2 not loaded yet, -1 none, else a gfx_dyn id */
static int overwrite;
static char message[4][48];
static int message_lines;
static int refresh_timer;

#define LIST_ROWS 7
#define ROW_H 34.0f
#define PANEL_X 56.0f
#define PANEL_Y 52.0f
#define PANEL_W 528.0f
#define PANEL_H 376.0f
#define LIST_X 84.0f
#define LIST_Y 124.0f
#define TEXT_COLOR 0xFFD0D0D0u

static const char* const slot_names[BFILES_SLOTS] = {"A1", "A2", "B1", "B2", "C1", "C2", "D1", "D2"};

static void
refresh_cards(void) {
    for (int i = 0; i < BFILES_SLOTS; i++) {
        grid.present[i] = vf_present(i);
        free_blocks[i] = grid.present[i] ? vf_free_blocks(i) : -1;
    }
}

static void
set_message(fstate next, int lines, const char* a, const char* b, const char* c) {
    const char* src[3] = {a, b, c};
    message_lines = lines;
    for (int i = 0; i < 3; i++) {
        snprintf(message[i], sizeof(message[i]), "%s", src[i] ? src[i] : "");
    }
    after_message = next;
    state = FS_MESSAGE;
    popup_sel = 0;
}

static void
free_file_icons(void) {
    gfx_dyn_free_all();
    for (int i = 0; i < VF_MAX_FILES; i++) {
        file_icon[i] = -2;
    }
    icon_page_base = -1;
}

static void
load_files(void) {
    free_file_icons();
    nfiles = vf_list(src_slot, files, VF_MAX_FILES);
    if (nfiles < 0) {
        nfiles = 0;
    }
    if (file_cursor >= nfiles) {
        file_cursor = nfiles > 0 ? nfiles - 1 : 0;
    }
    if (file_top > file_cursor) {
        file_top = file_cursor;
    }
    refresh_cards();
}

void
uif_set_pal(int pal) {
    grid.pal = pal;
}

void
uif_open(bmenu* m) {
    menu = m;
    int first = 0;
    bfiles_open(&grid, m, 0);
    refresh_cards();
    for (int i = BFILES_SLOTS - 1; i >= 0; i--) {
        if (grid.present[i]) {
            first = i;
        }
    }
    bfiles_set_cursor(&grid, first);
    state = FS_GRID;
    refresh_timer = 0;
    free_file_icons();
}

static void
move_file_cursor(int delta) {
    if (nfiles <= 0) {
        return;
    }
    file_cursor += delta;
    file_cursor = file_cursor < 0 ? 0 : (file_cursor > nfiles - 1 ? nfiles - 1 : file_cursor);
    if (file_cursor < file_top) {
        file_top = file_cursor;
    }
    if (file_cursor >= file_top + LIST_ROWS) {
        file_top = file_cursor - LIST_ROWS + 1;
    }
}

static void
grid_input(button_t b) {
    switch (b) {
        case BTN_UP:
            if (bfiles_move(&grid, 0, -1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_DOWN:
            if (bfiles_move(&grid, 0, 1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_LEFT:
            if (bfiles_move(&grid, -1, 0)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_RIGHT:
            if (bfiles_move(&grid, 1, 0)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        default: break;
    }
}

static void
start_op(int op) {
    busy_op = op;
    busy_frames = 0;
    state = FS_BUSY;
}

/* Result of a copy check or a copy as a message. Returns 1 if a message was set. */
static int
copy_problem(int code) {
    switch (code) {
        case VF_ERR_DEST: set_message(FS_DEST, 2, "The destination memory card is not ready.", "Unable to copy.", NULL); return 1;
        case VF_ERR_FULL: set_message(FS_DEST, 2, "The destination memory card is full.", "Unable to copy.", NULL); return 1;
        case VF_ERR_GAME:
            set_message(FS_DEST, 3, "A VMU game already exists on the destination", "memory card. Only one VMU game per", "memory card. Unable to copy.");
            return 1;
        case VF_ERR_PROTECTED: set_message(FS_BROWSE, 1, "This file cannot be copied.", NULL, NULL); return 1;
        default: return 0;
    }
}

int
uif_handle(button_t b) {
    switch (state) {
        case FS_GRID:
            if ((b == BTN_A || b == BTN_START) && grid.back_selected) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                gfx_dyn_free_all();
                return 1;
            } else if (b == BTN_A || b == BTN_START) {
                if (grid.present[grid.cursor]) {
                    src_slot = grid.cursor;
                    file_cursor = file_top = 0;
                    load_files();
                    state = FS_BROWSE;
                    sound_sfx(BAUDIO_SFX_ENTER);
                } else {
                    sound_sfx(BAUDIO_SFX_ERROR);
                    set_message(FS_GRID, 1, "No memory card found.", NULL, NULL);
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                gfx_dyn_free_all();
                return 1;
            } else {
                grid_input(b);
            }
            break;

        case FS_BROWSE:
            if (b == BTN_UP) {
                move_file_cursor(-1);
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_DOWN) {
                move_file_cursor(1);
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_LEFT || b == BTN_PAGE_UP) {
                move_file_cursor(-LIST_ROWS);
            } else if (b == BTN_RIGHT || b == BTN_PAGE_DOWN) {
                move_file_cursor(LIST_ROWS);
            } else if ((b == BTN_A || b == BTN_START) && nfiles > 0) {
                popup_sel = 0;
                state = FS_FILEMENU;
                sound_sfx(BAUDIO_SFX_ENTER);
            } else if (b == BTN_X) {
                popup_sel = 0;
                state = FS_CARDMENU;
                sound_sfx(BAUDIO_SFX_ENTER);
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                refresh_cards();
                free_file_icons();
                state = FS_GRID;
            }
            break;

        case FS_CARDMENU:
            if (b == BTN_UP && popup_sel > 0) {
                popup_sel--;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_DOWN && popup_sel < 3) {
                popup_sel++;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_A || b == BTN_START) {
                sound_sfx(BAUDIO_SFX_ENTER);
                if (vf_card_look(src_slot, &look_shape, &look_colour) != 0) {
                    look_shape = 0;
                    look_colour = 0;
                }
                if (popup_sel == 0) { /* Change icon */
                    picking_for_reset = 0;
                    icon_cursor = look_shape;
                    free_file_icons();
                    state = FS_ICONPICK;
                } else if (popup_sel == 1) { /* Change colour */
                    picking_for_reset = 0;
                    colour_cursor = look_colour;
                    state = FS_COLOURPICK;
                } else if (popup_sel == 2) { /* Memory reset */
                    popup_sel = 1; /* No */
                    state = FS_ASK_RESET;
                } else {
                    state = FS_BROWSE;
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                state = FS_BROWSE;
            }
            break;

        case FS_ICONPICK: {
            int step = b == BTN_LEFT ? -1 : (b == BTN_RIGHT ? 1 : (b == BTN_UP ? -8 : (b == BTN_DOWN ? 8 : 0)));
            if (step) {
                int next = icon_cursor + step;
                if (next >= 0 && next < VF_ICON_SHAPES) {
                    icon_cursor = next;
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
            } else if (b == BTN_A || b == BTN_START) {
                look_shape = icon_cursor;
                sound_sfx(BAUDIO_SFX_CONFIRM);
                if (picking_for_reset) {
                    colour_cursor = look_colour;
                    state = FS_COLOURPICK;
                } else {
                    start_op(2);
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                free_file_icons();
                state = FS_BROWSE;
            }
            break;
        }

        case FS_COLOURPICK:
            if (b == BTN_UP && colour_cursor > 0) {
                colour_cursor--;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_DOWN && colour_cursor < vf_colour_count() - 1) {
                colour_cursor++;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_A || b == BTN_START) {
                look_colour = colour_cursor;
                sound_sfx(BAUDIO_SFX_CONFIRM);
                if (picking_for_reset) {
                    popup_sel = 1; /* No */
                    state = FS_ASK_CONFIRM;
                } else {
                    start_op(2);
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                state = FS_BROWSE;
            }
            break;

        case FS_ASK_RESET:
        case FS_ASK_CONFIRM:
            if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT) {
                popup_sel = !popup_sel;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_A || b == BTN_START) {
                if (popup_sel == 0 && state == FS_ASK_RESET) { /* Yes: now choose the icon and colour of the new card */
                    picking_for_reset = 1;
                    icon_cursor = look_shape;
                    free_file_icons();
                    state = FS_ICONPICK;
                    sound_sfx(BAUDIO_SFX_ENTER);
                } else if (popup_sel == 0) { /* Yes: erase it */
                    start_op(3);
                } else {
                    sound_sfx(BAUDIO_SFX_CANCEL);
                    free_file_icons();
                    state = FS_BROWSE;
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                free_file_icons();
                state = FS_BROWSE;
            }
            break;

        case FS_FILEMENU:
            if (b == BTN_UP && popup_sel > 0) {
                popup_sel--;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_DOWN && popup_sel < 2) {
                popup_sel++;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_A || b == BTN_START) {
                if (popup_sel == 0) { /* Copy */
                    int others = 0;
                    for (int i = 0; i < BFILES_SLOTS; i++) {
                        others += grid.present[i] && i != src_slot;
                    }
                    if (files[file_cursor].protect) {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        set_message(FS_BROWSE, 1, "This file cannot be copied.", NULL, NULL);
                    } else if (!others) {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        set_message(FS_BROWSE, 1, "No memory card found.", NULL, NULL);
                    } else {
                        for (int i = BFILES_SLOTS - 1; i >= 0; i--) {
                            if (grid.present[i] && i != src_slot) {
                                bfiles_set_cursor(&grid, i);
                            }
                        }
                        state = FS_DEST;
                        sound_sfx(BAUDIO_SFX_ENTER);
                    }
                } else if (popup_sel == 1) { /* Delete */
                    popup_sel = 1;
                    state = FS_ASK_DELETE;
                    sound_sfx(BAUDIO_SFX_ENTER);
                } else {
                    state = FS_BROWSE;
                    sound_sfx(BAUDIO_SFX_CANCEL);
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                state = FS_BROWSE;
            }
            break;

        case FS_DEST:
            if (b == BTN_A || b == BTN_START) {
                int d = grid.cursor;
                if (!grid.present[d] || d == src_slot) {
                    sound_sfx(BAUDIO_SFX_ERROR);
                    break;
                }
                dst_slot = d;
                int check = vf_copy_check(src_slot, &files[file_cursor], dst_slot);
                if (check == VF_OK) {
                    overwrite = 0;
                    start_op(0);
                } else if (check == VF_ERR_EXISTS) {
                    popup_sel = 1; /* No */
                    state = FS_ASK_OVERWRITE;
                    sound_sfx(BAUDIO_SFX_ENTER);
                } else {
                    sound_sfx(BAUDIO_SFX_ERROR);
                    copy_problem(check);
                }
            } else if (b == BTN_B) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                state = FS_BROWSE;
            } else {
                grid_input(b);
            }
            break;

        case FS_ASK_OVERWRITE:
        case FS_ASK_DELETE:
            if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT) {
                popup_sel = !popup_sel;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (b == BTN_A || b == BTN_START) {
                if (popup_sel == 0) { /* Yes */
                    overwrite = state == FS_ASK_OVERWRITE;
                    start_op(state == FS_ASK_OVERWRITE ? 0 : 1);
                } else {
                    state = state == FS_ASK_OVERWRITE ? FS_DEST : FS_BROWSE;
                    sound_sfx(BAUDIO_SFX_CANCEL);
                }
            } else if (b == BTN_B) {
                state = state == FS_ASK_OVERWRITE ? FS_DEST : FS_BROWSE;
                sound_sfx(BAUDIO_SFX_CANCEL);
            }
            break;

        case FS_BUSY: break;

        case FS_MESSAGE:
            if (b == BTN_A || b == BTN_B || b == BTN_START) {
                state = after_message;
                sound_sfx(BAUDIO_SFX_CONFIRM);
            }
            break;
    }
    return 0;
}

void
uif_sync(void) {
    if (state == FS_BUSY && ++busy_frames >= 3) { /* the "do not remove" box has been on screen */
        int rc;
        if (busy_op == 0) {
            rc = vf_copy(src_slot, &files[file_cursor], dst_slot, overwrite);
            if (rc == VF_OK) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
                set_message(FS_BROWSE, 1, "File was copied.", NULL, NULL);
            } else if (!copy_problem(rc)) {
                sound_sfx(BAUDIO_SFX_ERROR);
                set_message(FS_BROWSE, 2, "File could not be copied.", "Please try again.", NULL);
            }
        } else if (busy_op == 2) {
            rc = vf_set_look(src_slot, look_shape, look_colour);
            if (rc == 0) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
                set_message(FS_BROWSE, 1, "The look of the memory card was changed.", NULL, NULL);
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
                set_message(FS_BROWSE, 2, "The memory card could not be changed.", "Please try again.", NULL);
            }
        } else if (busy_op == 3) {
            rc = vf_format(src_slot, look_shape, look_colour);
            if (rc == 0) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
                set_message(FS_BROWSE, 2, "All files were deleted and the", "memory card was reset.", NULL);
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
                set_message(FS_BROWSE, 2, "The memory card could not be reset.", "Check the card and try again.", NULL);
            }
        } else {
            rc = vf_delete(src_slot, &files[file_cursor]);
            if (rc == 0) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
                set_message(FS_BROWSE, 1, "File was deleted.", NULL, NULL);
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
                set_message(FS_BROWSE, 2, "File could not be deleted.", "Please try again.", NULL);
            }
        }
        load_files();
    }
    if (state == FS_BROWSE || state == FS_FILEMENU) { /* load the picture of one visible file a frame */
        for (int r = 0; r < LIST_ROWS && file_top + r < nfiles; r++) {
            int i = file_top + r;
            if (file_icon[i] == -2) {
                unsigned short px[32 * 32];
                file_icon[i] = vf_file_icon(src_slot, &files[i], px) == 0 ? gfx_dyn_create(px, 32, 32) : -1;
                break;
            }
        }
    }
    if (state == FS_ICONPICK && icon_cursor / 32 * 32 != icon_page_base) { /* the 32 icons of the page the cursor is on */
        for (int i = 0; i < 32; i++) {
            if (icon_page_base >= 0) {
                gfx_dyn_free(icon_tex_page[i]);
            }
        }
        icon_page_base = icon_cursor / 32 * 32;
        for (int i = 0; i < 32; i++) {
            unsigned short px[32 * 32];
            int shape = icon_page_base + i;
            icon_tex_page[i] = -1;
            if (shape < VF_ICON_SHAPES) {
                vf_icon_shape_picture(shape, 0xFFFF, 0xF223, px);
                icon_tex_page[i] = gfx_dyn_create(px, 32, 32);
            }
        }
    }
    if (state == FS_GRID || state == FS_DEST) {
        if (++refresh_timer >= 60) { /* cards come and go */
            refresh_timer = 0;
            refresh_cards();
        }
        bfiles_sync(&grid);
    }
}

/* ---- drawing -------------------------------------------------------------------------------- */

static void
text_centered(const char* s, float cx, float y, float z, uint32_t color) {
    gfx_text(s, cx - (float)gfx_text_width(s) / 2.0f, y, z, color, 1);
}

static void
panel(float x, float y, float w, float h) {
    bscene_draw_panel(&menu->scene, x, y, w, h, BMENU_ACCENT_FILES, gfx_sink());
}

static void
draw_list_popup(const char* title, const char* const* items, int n, int sel, unsigned grey_mask) {
    float w = 300.0f, h = 70.0f + (float)n * GFX_LINE_H;
    float x = 320.0f - w / 2.0f, y = 240.0f - h / 2.0f;
    panel(x, y, w, h);
    text_centered(title, 320.0f, y + 12.0f, 0.7f, 0xFFFFFFFFu);
    for (int i = 0; i < n; i++) {
        float ry = y + 50.0f + (float)i * GFX_LINE_H;
        if (i == sel) {
            gfx_rect(x + 14.0f, ry, w - 28.0f, (float)GFX_LINE_H, 0.65f, 0x60FFFFFFu);
        }
        gfx_text(items[i], x + 26.0f, ry, 0.7f, (grey_mask >> i) & 1 ? 0xFF808080u : TEXT_COLOR, 0);
    }
}

static void
draw_message(void) {
    float h = 60.0f + (float)message_lines * 30.0f;
    panel(70.0f, 240.0f - h / 2.0f, 500.0f, h);
    for (int i = 0; i < message_lines; i++) {
        text_centered(message[i], 320.0f, 240.0f - h / 2.0f + 20.0f + (float)i * 30.0f, 0.7f, 0xFFFFFFFFu);
    }
    gfx_text("A: OK", 320.0f + 160.0f, 240.0f + h / 2.0f - 36.0f, 0.7f, 0xFFA0A0A0u, 0);
}

static void
draw_grid_texts(void) {
    float x, y;
    const char* help = state == FS_DEST ? "Select the destination memory card and press A." : "Select a memory card and press A.";
    bfiles_title_px(&x, &y);
    text_centered(help, x, y - 16.0f, 0.7f, 0xFFFFFFFFu);
    char text[24];
    for (int i = 0; i < BFILES_SLOTS; i++) {
        float cx, cy;
        bfiles_card_px(i, &cx, &cy);
        if (grid.present[i] && free_blocks[i] >= 0) {
            snprintf(text, sizeof(text), "%d", free_blocks[i]);
            text_centered(text, cx, cy - 6.0f, 0.7f, 0xFF303030u);
        }
    }
    bfiles_plate_px(&x, &y);
    text_centered(slot_names[grid.cursor], x, y - 34.0f, 0.7f, 0xFFFFFFFFu);
    if (grid.present[grid.cursor] && free_blocks[grid.cursor] >= 0) {
        snprintf(text, sizeof(text), "%d", free_blocks[grid.cursor]);
        text_centered(text, x, y + 2.0f, 0.7f, 0xFFFFFFFFu);
        text_centered("free", x, y + 26.0f, 0.7f, 0xFFD0D0D0u);
    }
}

static void
draw_browser(void) {
    char line[48];
    panel(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    snprintf(line, sizeof(line), "Memory card %s", slot_names[src_slot]);
    gfx_text(line, LIST_X, 70.0f, 0.7f, 0xFFFFFFFFu, 1);
    snprintf(line, sizeof(line), "%d files, %d blocks free", nfiles, free_blocks[src_slot] < 0 ? 0 : free_blocks[src_slot]);
    gfx_text(line, LIST_X, 98.0f, 0.7f, 0xFFB0B0B0u, 0);
    if (nfiles == 0) {
        gfx_text("No files found.", LIST_X, LIST_Y + 20.0f, 0.7f, 0xFFD0D0D0u, 0);
    }
    for (int r = 0; r < LIST_ROWS && file_top + r < nfiles; r++) {
        const vf_file* f = &files[file_top + r];
        float y = LIST_Y + (float)r * ROW_H;
        int sel = file_top + r == file_cursor;
        if (sel) {
            gfx_rect(LIST_X - 12.0f, y, PANEL_W - 56.0f, ROW_H - 2.0f, 0.65f, 0x60FFFFFFu);
        }
        if (file_icon[file_top + r] >= 0) {
            gfx_image(file_icon[file_top + r], LIST_X - 6.0f, y, 32.0f, 32.0f, 0.7f);
        }
        gfx_text(f->name, LIST_X + 36.0f, y + 1.0f, 0.7f, sel ? 0xFFFFFFFFu : TEXT_COLOR, sel);
        snprintf(line, sizeof(line), "%3d", f->blocks);
        gfx_text(line, PANEL_X + PANEL_W - 120.0f, y + 1.0f, 0.7f, TEXT_COLOR, 0);
        if (f->is_game) {
            gfx_text("game", PANEL_X + PANEL_W - 200.0f, y + 1.0f, 0.7f, 0xFF90C0FFu, 0);
        }
    }
    gfx_text("A: file menu   X: card   B: back", LIST_X, PANEL_Y + PANEL_H - 40.0f, 0.7f, 0xFFA0A0A0u, 0);
}

static void
draw_icon_picker(void) {
    char line[48];
    panel(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    gfx_text(picking_for_reset ? "Choose an icon for the new card" : "Choose an icon", LIST_X, 70.0f, 0.7f, 0xFFFFFFFFu, 1);
    for (int i = 0; i < 32; i++) {
        int shape = icon_page_base + i;
        float x = LIST_X + (float)(i % 8) * 52.0f, y = 104.0f + (float)(i / 8) * 52.0f;
        if (shape == icon_cursor) {
            gfx_rect(x - 6.0f, y - 6.0f, 48.0f, 48.0f, 0.65f, 0x70FFFFFFu);
        }
        if (shape < VF_ICON_SHAPES && icon_page_base >= 0) {
            gfx_image(icon_tex_page[i], x, y, 36.0f, 36.0f, 0.7f);
        }
    }
    snprintf(line, sizeof(line), "%d / %d", icon_cursor + 1, VF_ICON_SHAPES);
    gfx_text(line, LIST_X, PANEL_Y + PANEL_H - 76.0f, 0.7f, 0xFFB0B0B0u, 0);
    gfx_text("A: choose   B: back", LIST_X, PANEL_Y + PANEL_H - 40.0f, 0.7f, 0xFFA0A0A0u, 0);
}

static void
draw_colour_picker(void) {
    panel(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    gfx_text(picking_for_reset ? "Choose a colour for the new card" : "Choose a colour", LIST_X, 70.0f, 0.7f, 0xFFFFFFFFu, 1);
    for (int i = 0; i < vf_colour_count(); i++) {
        float y = 104.0f + (float)i * 30.0f;
        if (i == colour_cursor) {
            gfx_rect(LIST_X - 12.0f, y - 2.0f, PANEL_W - 56.0f, 30.0f, 0.65f, 0x60FFFFFFu);
        }
        gfx_rect(LIST_X, y + 2.0f, 40.0f, 22.0f, 0.7f, vf_colour_argb(i));
        gfx_text(vf_colour_get(i)->name, LIST_X + 56.0f, y - 1.0f, 0.7f, i == colour_cursor ? 0xFFFFFFFFu : TEXT_COLOR, 0);
    }
    gfx_text("A: choose   B: back", LIST_X, PANEL_Y + PANEL_H - 40.0f, 0.7f, 0xFFA0A0A0u, 0);
}

void
uif_draw(void) {
    static const char* const card_menu[4] = {"Change icon", "Change colour", "Memory reset", "Cancel"};
    static const char* const file_menu[3] = {"Copy", "Delete", "Cancel"};
    static const char* const yes_no[2] = {"Yes", "No"};
    switch (state) {
        case FS_GRID:
        case FS_DEST:
            bfiles_draw(&grid, gfx_sink());
            draw_grid_texts();
            break;
        case FS_ICONPICK: draw_icon_picker(); break;
        case FS_COLOURPICK: draw_colour_picker(); break;
        default: draw_browser(); break;
    }
    switch (state) {
        case FS_FILEMENU:
            draw_list_popup(files[file_cursor].name, file_menu, 3, popup_sel, files[file_cursor].protect ? 1u : 0u);
            break;
        case FS_ASK_OVERWRITE: draw_list_popup("File exists. Overwrite?", yes_no, 2, popup_sel, 0); break;
        case FS_ASK_DELETE: draw_list_popup("Delete this file?", yes_no, 2, popup_sel, 0); break;
        case FS_CARDMENU: draw_list_popup(slot_names[src_slot], card_menu, 4, popup_sel, 0); break;
        case FS_ASK_RESET: draw_list_popup("Delete ALL files? Proceed?", yes_no, 2, popup_sel, 0); break;
        case FS_ASK_CONFIRM: draw_list_popup("Confirm your settings", yes_no, 2, popup_sel, 0); break;
        case FS_BUSY:
            message_lines = 2;
            snprintf(message[0], sizeof(message[0]), "%s",
                     busy_op == 0 ? "Copying..." : (busy_op == 1 ? "Deleting..." : (busy_op == 2 ? "Changing the card..." : "Deleting all...")));
            snprintf(message[1], sizeof(message[1]), "%s", "Please do not remove the memory card.");
            draw_message();
            break;
        case FS_MESSAGE: draw_message(); break;
        default: break;
    }
}

void
uif_hover(float x, float y) {
    if (state == FS_GRID || state == FS_DEST) {
        if (bfiles_back_at_px(x, y)) {
            if (!grid.back_selected) sound_sfx(BAUDIO_SFX_CURSOR);
            grid.back_selected = 1;
            return;
        }
        int slot = bfiles_slot_at_px(x, y);
        if (slot >= 0 && bfiles_set_cursor(&grid, slot)) {
            sound_sfx(BAUDIO_SFX_CURSOR);
        }
    } else if (state == FS_BROWSE) {
        if (x >= LIST_X - 12.0f && x <= PANEL_X + PANEL_W - 44.0f && y >= LIST_Y && y < LIST_Y + (float)LIST_ROWS * ROW_H) {
            int row = file_top + (int)((y - LIST_Y) / ROW_H);
            if (row < nfiles && row != file_cursor) {
                file_cursor = row;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
        }
    }
}
