/*
 * File: dcnow_fetch.h
 * Project: openmenu
 * Dreamcast Now! player list: HTTP fetch and JSON scan on a worker thread.
 */

#pragma once

#include <stdint.h>
#include <time.h>

#include "backend/dcnow_net.h"

#define DCNOW_PLAYER_MAX 128
#define DCNOW_NAME_LEN   32
#define DCNOW_TITLE_LEN  64

typedef struct dcnow_player {
    char name[DCNOW_NAME_LEN];
    char country[3];
    char title[DCNOW_TITLE_LEN];
    char code[16];
    dcnow_network_t network; /* which network this player is on, for the combined dc99.net list */
} dcnow_player_t;

typedef enum dcnow_fetch_state {
    DCNOW_FETCH_IDLE = 0,
    DCNOW_FETCH_RUNNING,
    DCNOW_FETCH_DONE,
    DCNOW_FETCH_FAILED
} dcnow_fetch_state_t;

/* Which site the player list comes from. DCNOW_ONLY is dreamcast.online's own
 * feed (DC Now players only, the official source). DC99_COMBINED is
 * dc99.net's community status page, which lists both DC Now and DCNet
 * players in one feed - useful since a modem connection only ever reaches
 * one network at a time, but dc99.net can still be reached over DC Now
 * (or an adapter) to see who's on either. */
typedef enum dcnow_fetch_source { DCNOW_FETCH_SRC_DCNOW_ONLY = 0, DCNOW_FETCH_SRC_DC99_COMBINED } dcnow_fetch_source_t;

typedef struct dcnow_fetch_status {
    dcnow_fetch_state_t state;
    int counter_line;       /* line with a live "(waiting N seconds)" counter, or -1 */
    uint64_t counter_since; /* when that counter started */
    char lines[DCNOW_STATUS_LINES][DCNOW_STATUS_WIDTH];
    int count;
    int generation;              /* grows by one for every list that arrived */
    int player_count;            /* online players in the newest list */
    int online_total;            /* online players in the feed, the list keeps at most DCNOW_PLAYER_MAX */
    time_t updated;              /* console clock when the newest list arrived */
    int list_valid;              /* a list is in the store, kept until the store is cleared */
    dcnow_fetch_source_t source; /* which source this list (or fetch in progress) came from */
} dcnow_fetch_status_t;

/* Starts a fetch of the given source's player list on the worker. Does
 * nothing while one is running. */
void dcnow_fetch_start(dcnow_fetch_source_t source);

/* Stops a running fetch and waits for the worker. The last list is kept. */
void dcnow_fetch_abort(void);

/* Forgets the list and the status, used when the connection goes away. */
void dcnow_fetch_clear(void);

/* Snapshot for the window, once per frame. */
void dcnow_fetch_poll(dcnow_fetch_status_t* out);

/* Copies the newest list. Returns how many players were copied. */
int dcnow_fetch_copy(dcnow_player_t* out, int max);
