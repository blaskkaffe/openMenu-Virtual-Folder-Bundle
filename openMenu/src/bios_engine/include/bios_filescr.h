/*
 * bios_filescr: the BIOS File screen (memory cards), ported from the boot ROM: file_screen_update 0x8C017A60 and
 * what it calls -- the card grid (vmu_card_select_grid 0x8C01F742), the file browser (vmu_file_browser_update
 * 0x8C01D5D8, vmu_info_panel 0x8C01E556, vmu_file_info_draw 0x8C01EB7C), the popup lists (popup_list_choose
 * 0x8C010DB0), the message boxes (message_box 0x8C011560), the copy box (vmu_progress_dialog 0x8C020340) and the
 * card zoom (file_zoom_card_anim 0x8C018B20). Same objects, scripts, ids, positions, text surfaces, cursor tables
 * and state machines as the original.
 *
 * The memory cards themselves are the caller's: it fills bfs_card (what the BIOS keeps in g_vmu) and carries
 * out the copies, deletions and memory resets the screen asks for (bfs_op). Portable C.
 */
#ifndef BIOS_FILESCR_H
#define BIOS_FILESCR_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BFS_SLOTS 8      /* slot = port * 2 + socket - 1: A1 A2 B1 B2 C1 C2 D1 D2 */
#define BFS_MAX_FILES 200

/* card status, as the BIOS keeps it (g_vmu + slot * 0x1E674 + 0x7F8) */
#define BFS_CARD_NONE 0
#define BFS_CARD_ERROR 2      /* removed, or it cannot be read */
#define BFS_CARD_READY 3
#define BFS_CARD_UNFORMATTED 4
#define BFS_CARD_READING 5    /* the files are being read: the card shows the VMU's busy animation */

typedef struct bfs_file {
    char name[13];   /* directory name */
    char vmdesc[17]; /* VMS header 0x00: the description shown on the VMU */
    char desc[33];   /* VMS header 0x10: the description for the boot ROM file manager */
    char app[17];    /* VMS header 0x30: the application that made the file (files are sorted by it) */
    int blocks;
    uint8_t time[8]; /* directory time stamp, BCD: century, year, month, day, hour, minute, second, weekday */
    uint8_t protect; /* 0xFF: copy protected */
    uint8_t game;    /* a VMU game (file type 0xCC) */
    uint8_t header;  /* 2 once the VMS header (and icon) has been read; the tile shows no icon before */
    uint8_t icons;   /* icon frames (0..3) */
    uint8_t eyecatch;/* eyecatch type 0..3 */
    uint8_t frame;   /* the icon frame shown (animated by the screen) */
    int16_t speed;   /* frames per icon frame */
    int16_t wait;
    uint16_t palette[16];    /* ARGB4444 */
    const uint8_t* bitmaps;  /* icons * 512 bytes, 32x32 at 4 bits a pixel; the caller's memory */
} bfs_file;

typedef struct bfs_card {
    int status;          /* BFS_CARD_* */
    uint32_t colour;     /* ARGB of the card's body: the custom colour of the root block, 0xFFFFFFFF none */
    uint16_t icon[32 * 32]; /* the card's own picture (ICONDATA_VMS) as ARGB4444, see bfs_mono_icon() */
    int free_blocks, total_blocks;
    int nfiles;
    bfs_file* files;     /* the caller's array of BFS_MAX_FILES */
    uint32_t serial;     /* bump when anything above changes (the screen redraws what shows the card) */
} bfs_card;

/* The picture of a monochrome 32x32 VMU icon as the BIOS shows it (vmu_icon_to_texture 0x8C0220C0): set pixels
 * `fg` (0xF225 normally, 0xFD00 for an unformatted card), the others 0xFBC6. */
void bfs_mono_icon(const uint8_t bits[128], uint16_t fg, uint16_t out[32 * 32]);

/* What the screen asks the caller to do */
#define BFS_OP_NONE 0
#define BFS_OP_COPY 1    /* files[] of card src to card dst, overwriting files of the same name */
#define BFS_OP_DELETE 2  /* files[] of card src */
#define BFS_OP_FORMAT 3  /* memory reset of card src with icon `shape` and colour `colour` (0xAARRGGBB, 0 = none) */

typedef struct bfs_op {
    int kind;
    int src, dst;
    int files[BFS_MAX_FILES]; /* indices into the card's files */
    int nfiles;
    int shape;
    uint32_t colour;
    /* the caller: */
    int current;  /* the file being copied (index into files[]), for its name in the copy box */
    int progress; /* 0..1000 */
    int done;     /* set when finished; the cards' data must be up to date by then */
    int failed;
} bfs_op;

typedef struct bfs_host {
    void* user;
    void (*sfx)(void* user, int id); /* ui_sfx: BAUDIO_SFX_* numbers */
    /* The eyecatch of a file as 72x56 ARGB4444. Returns 0 if there is one. May be NULL. */
    int (*eyecatch)(void* user, int slot, int file, uint16_t out[72 * 56]);
} bfs_host;

#define BFS_KEY_UP 1
#define BFS_KEY_DOWN 2
#define BFS_KEY_LEFT 3
#define BFS_KEY_RIGHT 4
#define BFS_KEY_A 5
#define BFS_KEY_B 6
#define BFS_KEY_X 7
#define BFS_KEY_Y 8

struct bfs;

typedef struct bfs_cursor {
    const signed char (*table)[4]; /* up, down, left, right */
    int count, pos, on;
    void (*cb)(struct bfs* fs, int code, int* pos);
} bfs_cursor;

typedef struct bfs_popup_def {
    int w, h, tw, th, tx, ty, bx, by, spacing;
    int px, py, pz; /* position * 256 */
    int count, cancel;
    struct {
        int grey, msg;
    } item[4];
} bfs_popup_def;

typedef struct bfs {
    bmenu* m;
    const bios_rom* rom;
    bfs_host host;
    bfs_card* card; /* BFS_SLOTS cards */
    int ports;      /* bit n: a controller in port n */
    int language;   /* 1 English, see btext_message() */
    int date_order; /* 0 year first, 1 month first, 2 day first */
    int pal;
    int anim;

    int state;            /* g_file_state */
    int mode;             /* g_file_mode: 0 one file, 1 several, 2 all of the card */
    int src, dst;         /* g_vmu + 4, + 6 */
    int src_watch, dst_watch; /* DAT_8C38143C / 3E: cards whose removal stops the command */
    int changed;          /* DAT_8C381440 */
    int list[BFS_MAX_FILES], nlist; /* g_vmu + 0x1E, + 0x1A: the files of the command */
    int browse_card;      /* DAT_8C381442: whose file headers are wanted, -1 none */

    /* card grid */
    int g_state, g_result, g_cursor, g_last, g_dest, g_help, g_tick, g_busy_frame, g_ready;
    /* browser */
    int b_state, b_result, b_mode, b_card, b_page, b_cursor, b_blink, b_blink_t, b_same, b_redraw, b_dirty_info;
    int b_saved_page, b_saved_cursor;
    int order[BFS_MAX_FILES];   /* g_vmu 0x389E: display order */
    uint8_t marked[BFS_MAX_FILES]; /* 0x406E: by display position */
    int protected_shown;        /* DAT_8C06F5B8 */
    int eye_slot, eye_file, eye_ok;
    uint16_t eye[72 * 56];
    /* popups and messages */
    int p_state, p_result;
    const bfs_popup_def* p_def;
    int p_help;
    int m_state, m_result;
    /* screen_fly_in / out */
    int fly, fly_alpha;
    bfs_cursor cur[2];
    int layer;
    int key;
    /* tables from the ROM */
    const uint8_t* grid_search;  /* 0x8C036FE0 */
    const uint8_t* arrow_tab;    /* 0x8C037124 */
    const uint8_t* arrow_sx;     /* 0x8C037164 */
    const uint8_t* arrow_sy;     /* 0x8C037264 */
    uint32_t card_sig[BFS_SLOTS + 1];
    int plate_sig;
    int gauge_set;               /* the plate's gauge as last set (to clear the scene cache when it changes) */
    float gauge;

    /* memory reset (vmu_format_flow): the icon and colour pickers */
    int f_state, f_page, f_icon, f_cursor, f_transparent, f_blink, f_blink_t;
    uint32_t f_colour;

    bfs_op op;
} bfs;

void bfs_init(bfs* fs, bmenu* m, const bfs_host* host, bfs_card cards[BFS_SLOTS]);
/* Enter the screen (file_screen_enter): replaces the scene. */
void bfs_open(bfs* fs);
/* A button (BFS_KEY_*), handled on the next bfs_update(). */
void bfs_key(bfs* fs, int key);
/* One frame, after bmenu_update(). Returns 1 when the screen has been left. */
int bfs_update(bfs* fs);
void bfs_draw(bfs* fs, const bscene_sink* sink);
/* The command the caller has to carry out, NULL if none. */
bfs_op* bfs_pending(bfs* fs);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_FILESCR_H */
