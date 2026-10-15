/*
 * File: events_fetch.h
 * Project: openmenu
 * Upcoming DC99 community events for the Internet screen, read on a worker thread: from the DreamPi add-on's events
 * module (GET /api/events/upcoming) when the console is online through it, otherwise from the dc99.net community
 * page itself (the JSON list in it, plain HTTP).
 */
#pragma once

#include <stdint.h>

#define EVENTS_MAX       40
#define EVENT_TITLE_LEN  64
#define EVENT_WHEN_LEN   24
#define EVENT_SOURCE_LEN 24

typedef struct event_item {
    char title[EVENT_TITLE_LEN];
    char when[EVENT_WHEN_LEN];     /* "Sat 17 Oct 21:00" (+ " ET" / " UK" when the time zone is the source's own) */
    char source[EVENT_SOURCE_LEN]; /* "Sega Online Discord", "DreamcastLive", "DC99" */
    char sort_key[20];             /* "YYYY-MM-DD HH:MM" for ordering */
} event_item_t;

typedef enum events_state { EVENTS_IDLE = 0, EVENTS_RUNNING, EVENTS_DONE, EVENTS_FAILED } events_state_t;

typedef struct events_status {
    events_state_t state;
    int generation;  /* grows by one for every list that arrived */
    int count;
    int from_pi;     /* the list came from the DreamPi add-on (else from dc99.net) */
    char line[48];   /* what is going on, or why it failed */
} events_status_t;

/* Starts a fetch on the worker. Does nothing while one is running. */
void events_fetch_start(void);
/* Stops a running fetch and waits for the worker. The last list is kept. */
void events_fetch_abort(void);
/* Snapshot of the state, once per frame. */
void events_fetch_poll(events_status_t* out);
/* Copies the newest list (soonest first). Returns how many were copied. */
int events_fetch_copy(event_item_t* out, int max);
