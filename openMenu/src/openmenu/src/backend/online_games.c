/*
 * File: online_games.c
 * Project: openmenu
 * Which games on the card somebody is playing online right now. With the DreamPi add-on the Pi says so (its PLY line); otherwise the
 * player list the Dreamcast Now! window shows is used. A game
 * matches a player's title when the two names agree once case, punctuation and bracketed parts such as "(USA)" are ignored, or, for
 * names of six or more letters, when one holds the other ("Sonic Adventure 2" and "Sonic Adventure 2 Battle").
 */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <arch/timer.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>
#include <openmenu_settings.h>

#include "backend/dcnow_fetch.h"
#include "backend/dcnow_net.h"
#include "backend/dreampi_link.h"
#include "backend/online_games.h"

#define NORM_LEN   48
#define TITLE_MAX  96
#define MIN_LOOSE  6

static dcnow_player_t players[DCNOW_PLAYER_MAX];
static char titles[TITLE_MAX][NORM_LEN];
static int title_players[TITLE_MAX];
static int title_count = 0;
static uint8_t* flags = NULL; /* players per game, by position in the full game list */
static int flags_len = 0;
static int seen_generation = -1;
static uint64_t last_fetch_started = 0;

/* Lower case letters and digits only, with anything inside () or [] left out. */
static void
normalize(const char* in, char* out, size_t out_len) {
    size_t n = 0;
    int depth = 0;

    for (; *in != '\0' && n < out_len - 1; in++) {
        if (*in == '(' || *in == '[') {
            depth++;
        } else if (*in == ')' || *in == ']') {
            if (depth > 0) {
                depth--;
            }
        } else if (depth == 0 && isalnum((unsigned char)*in)) {
            out[n++] = (char)tolower((unsigned char)*in);
        }
    }
    out[n] = '\0';
}

static int
names_match(const char* game, const char* title) {
    const size_t gl = strlen(game);
    const size_t tl = strlen(title);

    if (gl == 0 || tl == 0) {
        return 0;
    }
    if (gl == tl && strcmp(game, title) == 0) {
        return 1;
    }
    return gl >= MIN_LOOSE && tl >= MIN_LOOSE && (strstr(title, game) != NULL || strstr(game, title) != NULL);
}

/* Counts the players per distinct title, then walks the game list once. */
static void
rebuild(void) {
    const int count = list_all_count();
    const int got = dcnow_fetch_copy(players, DCNOW_PLAYER_MAX);
    char norm[NORM_LEN];

    title_count = 0;
    for (int i = 0; i < got; i++) {
        int found = -1;

        if (players[i].title[0] == '\0') {
            continue;
        }
        normalize(players[i].title, norm, sizeof(norm));
        if (norm[0] == '\0') {
            continue;
        }
        for (int t = 0; t < title_count; t++) {
            if (strcmp(titles[t], norm) == 0) {
                found = t;
                break;
            }
        }
        if (found < 0 && title_count < TITLE_MAX) {
            found = title_count++;
            memcpy(titles[found], norm, sizeof(norm));
            title_players[found] = 0;
        }
        if (found >= 0) {
            title_players[found]++;
        }
    }

    if (flags == NULL || flags_len != count) {
        free(flags);
        flags = count > 0 ? calloc((size_t)count, 1) : NULL;
        flags_len = flags != NULL ? count : 0;
    }
    if (flags == NULL) {
        return;
    }
    memset(flags, 0, (size_t)flags_len);
    if (title_count == 0) {
        return;
    }
    for (int g = 1; g < flags_len; g++) {
        const gd_item* item = list_all_item(g);
        int total = 0;

        if (item == NULL || item->product[0] == '\0' || strcmp(item->disc, "DIR") == 0) {
            continue;
        }
        normalize(item->name, norm, sizeof(norm));
        for (int t = 0; t < title_count; t++) {
            if (names_match(norm, titles[t])) {
                total += title_players[t];
            }
        }
        flags[g] = total > 255 ? 255 : (uint8_t)total;
    }
}

void
online_games_tick(void) {
    dcnow_status_t status;
    dcnow_fetch_status_t fetch;

    if (sf_dcnow[0] == DCNOW_OFF) {
        if (flags != NULL && seen_generation != -2) {
            memset(flags, 0, (size_t)flags_len);
            seen_generation = -2;
        }
        return;
    }
    dcnow_conn_poll(&status);
    dcnow_fetch_poll(&fetch);

    /* A DreamPi with the add-on says which games are played in its answers, so the player list is not needed for the marks. */
    if (dreampi_link_playing_known()) {
        return;
    }

    /* With Auto-Refresh on, the list stays fresh without the window: first when a connection comes up, then every interval. Any
     * fetch, the window's or the VMU screen's included, restarts the interval. */
    if (fetch.state == DCNOW_FETCH_RUNNING) {
        last_fetch_started = timer_ms_gettime64();
    }
    if (status.state == DCNOW_CONN_ONLINE && fetch.state != DCNOW_FETCH_RUNNING) {
        const int seconds = dcnow_refresh_seconds(sf_dcnow_refresh[0]);
        const uint64_t now = timer_ms_gettime64();

        if (seconds > 0 && ((!fetch.list_valid && fetch.state == DCNOW_FETCH_IDLE)
                            || (fetch.list_valid && now - last_fetch_started >= (uint64_t)seconds * 1000))) {
            last_fetch_started = now;
            dcnow_fetch_start();
        }
    }

    if (fetch.generation != seen_generation) {
        seen_generation = fetch.generation;
        rebuild();
    }
}

int
online_games_players(const gd_item* item) {
    /* The Pi's own account wins: it knows the games by their place on the card, and how many players are in each. */
    if (dreampi_link_playing_known()) {
        return dreampi_link_game_playing(item);
    }

    const uintptr_t base = (uintptr_t)list_all_item(0);
    const uintptr_t at = (uintptr_t)item;

    if (flags == NULL || item == NULL || base == 0 || at < base) {
        return 0;
    }
    const uintptr_t index = (at - base) / sizeof(gd_item);

    return (at - base) % sizeof(gd_item) == 0 && index < (uintptr_t)flags_len ? flags[index] : 0;
}
