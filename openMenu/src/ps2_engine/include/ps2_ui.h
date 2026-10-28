/*
 * ps2_ui: the PS2-style menu as a portable state machine: main menu (Browser, Settings), the
 * device screen (memory cards, disc), the game list of the disc as a row of cubes, the memory
 * card screens and the settings list. It knows nothing about games, settings or the console: the
 * host supplies those through ps2_host and gets triangles through ps2_draw's sink, so the same
 * code runs on the Dreamcast and in the PC preview tool. Portable C.
 *
 * All animation runs on a time accumulator in seconds, as the timings recovered from the PS2
 * BIOS are given in seconds (video fields at 50/60 Hz).
 */
#ifndef PS2_UI_H
#define PS2_UI_H

#include <stddef.h>

#include "ps2_draw.h"
#include "ps2_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PS2_BTN_NONE,
    PS2_BTN_UP,
    PS2_BTN_DOWN,
    PS2_BTN_LEFT,
    PS2_BTN_RIGHT,
    PS2_BTN_A,     /* enter / start */
    PS2_BTN_B,     /* back */
    PS2_BTN_X,     /* options: recently played */
    PS2_BTN_START
} ps2_btn;

typedef enum { PS2_SFX_CURSOR, PS2_SFX_CONFIRM, PS2_SFX_CANCEL, PS2_SFX_ENTER, PS2_SFX_ERROR } ps2_sfx;

/* What the game list is asked to do (the host maps it on its list navigation) */
typedef enum {
    PS2_NAV_PREV,       /* one game back */
    PS2_NAV_NEXT,       /* one game on */
    PS2_NAV_PAGE_PREV,  /* previous disc of a set, else a page back */
    PS2_NAV_PAGE_NEXT,
    PS2_NAV_ENTER,      /* open a folder or start the game */
    PS2_NAV_BACK        /* leave a folder; at the top level the list is left */
} ps2_nav;

typedef enum { PS2_NAVR_NONE, PS2_NAVR_CHANGED, PS2_NAVR_LAUNCH, PS2_NAVR_EXIT } ps2_nav_result;

typedef struct ps2_game_entry {
    char name[64];
    char info[48];     /* second line: product code, region, ... */
    char product[16];  /* key of the picture, empty if none */
    int is_folder;
    int disc_index;    /* picked disc of a set (0-based) */
    int disc_total;    /* discs of the set, 1 if it is not one */
} ps2_game_entry;

typedef struct ps2_host {
    void* user;
    /* game list of the disc */
    int (*game_count)(void* user);
    int (*game_cursor)(void* user);
    void (*game_get)(void* user, int index, ps2_game_entry* out);
    int (*game_nav)(void* user, int nav);                /* returns ps2_nav_result */
    void (*game_folder)(void* user, char* out, size_t n); /* name of the open folder, "" at the top */
    void (*game_opened)(void* user);                     /* the game list screen is opened (restore the last game) */
    void (*game_launch)(void* user);                     /* start the game of the last PS2_NAVR_LAUNCH; returns on failure */
    int (*launch_anim)(void* user);                      /* 1: show the cube flying up before the game starts */
    /* recently played (popup opened with X) */
    int (*recent_count)(void* user);
    const char* (*recent_name)(void* user, int index);
    void (*recent_launch)(void* user, int index);
    /* settings */
    int (*set_count)(void* user);
    const char* (*set_label)(void* user, int row);
    const char* (*set_group)(void* user, int row);
    const char* (*set_help)(void* user, int row);
    const char* (*set_value)(void* user, int row);
    int (*set_choice_count)(void* user, int row);
    const char* (*set_choice_name)(void* user, int row, int choice);
    int (*set_get)(void* user, int row);
    void (*set_set)(void* user, int row, int choice);
    int (*set_opens_popup)(void* user, int row);
    void (*set_change)(void* user, int row, int delta);
    int (*set_leave)(void* user);                        /* save; returns 0 on success */
    /* misc */
    int (*memcard_present)(void* user, int slot);        /* slot 0 or 1 */
    void (*clock_text)(void* user, char* out, size_t n);
    void (*sfx)(void* user, int sfx);
} ps2_host;

typedef enum {
    PS2_SCREEN_MAIN,
    PS2_SCREEN_DEVICES,
    PS2_SCREEN_GAMES,
    PS2_SCREEN_CARD,
    PS2_SCREEN_SETTINGS
} ps2_screen;

#define PS2_UI_IDS 40

typedef struct ps2_ui {
    const ps2_pack* pack;
    ps2_host host;
    ps2_draw draw;

    /* texture / model indexes, -1 if the pack lacks them */
    struct {
        int fog[5], flare, ref, blur, smoke, glow1, glow2, ring, btn, arrow, kabe, wall;
        int m_disc, m_card, m_cube, m_system;
        int t_disc, t_card, t_cube, t_folder, t_system;
    } id;

    float time;
    int screen, pending_screen;
    float trans;       /* >0: leaving the old screen, counts down; <0: arriving, counts up to 0 */
    int transitioning; /* 0 none, 1 leaving, 2 arriving */
    float boot_t;      /* start-up fade from black */

    int main_sel;
    float main_pos;
    int dev_sel;
    float dev_pos;
    int card_slot;
    float game_pos;    /* eased cursor of the game row */
    int game_count_seen;
    char game_folder_seen[64];
    int settings_cursor, settings_top;
    int popup;         /* settings choice popup open */
    int popup_sel;
    int recent_open, recent_sel;
    float launch_t;    /* >0 while the launch animation runs */
    int launching;
    char notice[64];
    float notice_t;
    char clock[24];
    float clock_t;
} ps2_ui;

/* `pack` must stay valid (and its buffer) while the ui is used. */
int ps2_ui_init(ps2_ui* ui, const ps2_pack* pack, const ps2_host* host, const ps2_sink* sink);

void ps2_ui_button(ps2_ui* ui, ps2_btn btn);
void ps2_ui_update(ps2_ui* ui, float dt);
void ps2_ui_draw(ps2_ui* ui);

/* Colours of the flat gradient the host clears the frame with before ps2_ui_draw(). */
void ps2_ui_background(uint32_t* top, uint32_t* bottom);

/* Jump straight to a screen (preview and tests); no animation. */
void ps2_ui_show(ps2_ui* ui, int screen);

#ifdef __cplusplus
}
#endif

#endif /* PS2_UI_H */
