/*
 * File: news_fetch.h
 * Project: openmenu
 * Community news for the Internet screen, read on a worker thread from the DreamcastLive and Sega Online pages (plain HTTP, made
 * for the Dreamcast) and cut into items by news_parse.
 */
#pragma once

#include "backend/news_parse.h"

typedef enum news_state { NEWS_IDLE = 0, NEWS_RUNNING, NEWS_DONE, NEWS_FAILED } news_state_t;

typedef struct news_status {
    news_state_t state;
    int generation; /* grows by one for every list that arrived */
    int count;
    char line[48];  /* what is going on, or why it failed */
} news_status_t;

/* Starts a fetch on the worker. Does nothing while one is running. */
void news_fetch_start(void);
/* Stops a running fetch and waits for the worker. The last list is kept. */
void news_fetch_abort(void);
/* Snapshot of the state, once per frame. */
void news_fetch_poll(news_status_t* out);
/* Copies the newest list. Returns how many were copied. */
int news_fetch_copy(news_item_t* out, int max);
