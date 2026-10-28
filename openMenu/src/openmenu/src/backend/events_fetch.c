/*
 * File: events_fetch.c
 * Project: openmenu
 * See events_fetch.h. The HTTP and JSON helpers follow dcnow_fetch.c.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "backend/dcnow_net.h"
#include "backend/dreampi_link.h"
#include "backend/events_fetch.h"

#define DC99_HOST       "dc99.net"
#define DC99_PATH       "/community/"
#define PI_PATH         "/api/events/upcoming?limit=40"
#define BODY_MAX        (160 * 1024) /* the community page is about 84 KB */
#define PI_TIMEOUT_MS   15000
#define WEB_TIMEOUT_MS  30000

static mutex_t events_mutex = MUTEX_INITIALIZER;
static events_status_t status;
static event_item_t items[EVENTS_MAX];
static event_item_t parsed[EVENTS_MAX];
static kthread_t* worker = NULL;
static volatile int abort_requested = 0;
static volatile int worker_done = 0;
static int restart_pending; /* started again while an aborted fetch was still ending: starts when it has */
static char body[BODY_MAX + 1];

static void
set_line(events_state_t state, const char* text) {
    mutex_lock(&events_mutex);
    status.state = state;
    snprintf(status.line, sizeof(status.line), "%s", text);
    mutex_unlock(&events_mutex);
}

/* ---- HTTP (as dcnow_fetch.c) ---------------------------------------------------------------------------------------- */

static int
wait_socket(int fd, short events, uint64_t deadline) {
    struct pollfd pfd;
    while (timer_ms_gettime64() < deadline) {
        if (abort_requested) {
            return -1;
        }
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;
        if (poll(&pfd, 1, 100) > 0) {
            return pfd.revents;
        }
    }
    return 0;
}

/* One plain HTTP GET into body[]. Returns the status code, -1 on any failure and -2 when aborted. */
static int
http_get(const struct sockaddr_in* addr, const char* host, const char* path, uint64_t timeout_ms, size_t* used) {
    char request[192];
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    size_t sent = 0, len;
    int code = -1, fd, rc;

    *used = 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    rc = connect(fd, (const struct sockaddr*)addr, sizeof(*addr));
    if (rc < 0 && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != EAGAIN) {
        close(fd);
        return -1;
    }
    rc = wait_socket(fd, POLLWRNORM, deadline);
    if (rc <= 0 || (rc & (POLLHUP | POLLERR))) {
        close(fd);
        return rc < 0 ? -2 : -1;
    }
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\nAccept: application/json, text/html\r\nConnection: close\r\n\r\n",
             path, host);
    len = strlen(request);
    while (sent < len) {
        ssize_t n = send(fd, request + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        rc = (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) ? wait_socket(fd, POLLWRNORM, deadline) : 0;
        if (rc <= 0) {
            close(fd);
            return rc < 0 ? -2 : -1;
        }
    }
    for (;;) {
        rc = wait_socket(fd, POLLRDNORM, deadline);
        if (rc <= 0) {
            close(fd);
            return rc < 0 ? -2 : -1;
        }
        ssize_t got = recv(fd, body + *used, BODY_MAX - *used, 0);
        if (got > 0) {
            *used += (size_t)got;
            if (*used == BODY_MAX) {
                break;
            }
            continue;
        }
        if (got < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(rc & POLLHUP)) {
            continue;
        }
        break;
    }
    close(fd);
    body[*used] = '\0';
    if (sscanf(body, "HTTP/%*d.%*d %d", &code) != 1) {
        return -1;
    }
    return code;
}

/* ---- JSON (a scanner, as dcnow_fetch.c) ------------------------------------------------------------------------------ */

static const char*
json_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/* Copies a JSON string, simple escapes decoded, anything outside ASCII dropped (the fonts stop there). */
static const char*
json_string(const char* p, char* out, size_t out_len) {
    size_t n = 0;
    if (*p != '"') {
        return NULL;
    }
    p++;
    while (*p != '\0' && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            if (e == '\0') {
                return NULL;
            }
            if (e == 'u') {
                unsigned int code = 0;
                for (int i = 0; i < 4; i++) {
                    if (!isxdigit((unsigned char)p[i])) {
                        return NULL;
                    }
                    code = code * 16 + (unsigned int)(isdigit((unsigned char)p[i]) ? p[i] - '0' : tolower((unsigned char)p[i]) - 'a' + 10);
                }
                p += 4;
                if (code >= 0x80) {
                    continue;
                }
                c = (char)code;
            } else if (e == 'n' || e == 'r' || e == 't' || e == 'b' || e == 'f') {
                c = ' ';
            } else {
                c = e;
            }
        } else if ((unsigned char)c >= 0x80) {
            while (((unsigned char)*p & 0xC0) == 0x80) {
                p++;
            }
            continue;
        }
        if (out != NULL && n + 1 < out_len) {
            out[n++] = c;
        }
    }
    if (out != NULL && out_len > 0) {
        out[n] = '\0';
    }
    return *p == '"' ? p + 1 : NULL;
}

static const char*
json_skip(const char* p) {
    int depth = 0;
    p = json_ws(p);
    if (*p == '"') {
        return json_string(p, NULL, 0);
    }
    if (*p == '{' || *p == '[') {
        do {
            if (*p == '"') {
                p = json_string(p, NULL, 0);
                if (p == NULL) {
                    return NULL;
                }
                continue;
            }
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                depth--;
            } else if (*p == '\0') {
                return NULL;
            }
            p++;
        } while (depth > 0);
        return p;
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return *p == '\0' ? NULL : p;
}

/* The fields of one event object (both sources use title; the time and source fields differ). */
typedef struct {
    char title[EVENT_TITLE_LEN];
    char time[32];   /* "2026-10-17T21:00:00+02:00" (DreamPi) or "2026-10-17 21:00:00" (dc99.net) */
    char source[EVENT_SOURCE_LEN];
    int removed;
} raw_event;

static const char*
json_event(const char* p, raw_event* out, const char* time_key, const char* source_key) {
    char key[32];
    memset(out, 0, sizeof(*out));
    p = json_ws(p);
    if (*p != '{') {
        return NULL;
    }
    p = json_ws(p + 1);
    while (*p != '}') {
        p = json_string(p, key, sizeof(key));
        if (p == NULL) {
            return NULL;
        }
        p = json_ws(p);
        if (*p != ':') {
            return NULL;
        }
        p = json_ws(p + 1);
        if (!strcmp(key, "title") && *p == '"') {
            p = json_string(p, out->title, sizeof(out->title));
        } else if (!strcmp(key, time_key) && *p == '"') {
            p = json_string(p, out->time, sizeof(out->time));
        } else if (!strcmp(key, source_key) && *p == '"') {
            p = json_string(p, out->source, sizeof(out->source));
        } else if (!strcmp(key, "removed")) {
            out->removed = !strncmp(p, "true", 4);
            p = json_skip(p);
        } else {
            p = json_skip(p);
        }
        if (p == NULL) {
            return NULL;
        }
        p = json_ws(p);
        if (*p == ',') {
            p = json_ws(p + 1);
        } else if (*p != '}') {
            return NULL;
        }
    }
    return p + 1;
}

/* Day of the week of a date, 0 = Sunday (Sakamoto). */
static int
weekday(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y--;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7;
}

/* "YYYY-MM-DD?HH:MM..." into the display text and the sort key. 0 when it is not a time. */
static int
format_when(const char* text, const char* zone_note, event_item_t* out) {
    static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, mo, d, h, mi;
    if (sscanf(text, "%4d-%2d-%2d%*c%2d:%2d", &y, &mo, &d, &h, &mi) != 5 || mo < 1 || mo > 12 || d < 1 || d > 31) {
        return 0;
    }
    snprintf(out->when, sizeof(out->when), "%s %d %s %02d:%02d%s", days[weekday(y, mo, d)], d, months[mo - 1], h, mi, zone_note);
    snprintf(out->sort_key, sizeof(out->sort_key), "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi);
    return 1;
}

static int
by_time(const void* a, const void* b) {
    return strcmp(((const event_item_t*)a)->sort_key, ((const event_item_t*)b)->sort_key);
}

/* Reads the array that starts at the first '[' after `marker`. Returns how many events it kept, -1 if there is none. */
static int
parse_events(const char* text, const char* marker, int from_pi) {
    const char* p = strstr(text, marker);
    char today[16];
    time_t now = time(NULL);
    struct tm* t = gmtime(&now); /* the menu treats the console clock as the local time */
    int count = 0;

    snprintf(today, sizeof(today), "%04d-%02d-%02d", t ? t->tm_year + 1900 : 2000, t ? t->tm_mon + 1 : 1, t ? t->tm_mday : 1);
    if (p == NULL || (p = strchr(p, '[')) == NULL) {
        return -1;
    }
    p = json_ws(p + 1);
    while (*p != ']' && *p != '\0') {
        raw_event e;
        p = json_event(p, &e, from_pi ? "start_time" : "date", from_pi ? "source_label" : "source");
        if (p == NULL) {
            break; /* cut short: keep what was read */
        }
        p = json_ws(p);
        if (*p == ',') {
            p = json_ws(p + 1);
        }
        if (e.removed || e.title[0] == '\0') {
            continue;
        }
        event_item_t it;
        memset(&it, 0, sizeof(it));
        snprintf(it.title, sizeof(it.title), "%s", e.title);
        if (from_pi) {
            /* the add-on gives the time in the Pi's own zone, with the offset: shown as it is */
            snprintf(it.source, sizeof(it.source), "%s", e.source);
            if (!format_when(e.time, "", &it)) {
                continue;
            }
        } else {
            /* dc99.net: no time zone; US Eastern, UK time for an event with "UK" in its title (as the add-on reads it) */
            const char* uk = strstr(e.title, "UK");
            int is_uk = uk != NULL && (uk == e.title || !isalnum((unsigned char)uk[-1])) && !isalnum((unsigned char)uk[2]);
            snprintf(it.source, sizeof(it.source), "%s",
                     !strcmp(e.source, "discord") ? "Sega Online Discord"
                     : !strcmp(e.source, "dreamcastlive") ? "DreamcastLive"
                     : !strcmp(e.source, "manual") ? "DC99" : e.source);
            if (!format_when(e.time, is_uk ? " UK" : " ET", &it) || strncmp(it.sort_key, today, 10) < 0) {
                continue; /* not a time, or before today */
            }
        }
        if (count < EVENTS_MAX) {
            parsed[count++] = it;
        } else {
            /* the page lists a year ahead: keep the soonest */
            qsort(parsed, (size_t)count, sizeof(parsed[0]), by_time);
            if (strcmp(it.sort_key, parsed[count - 1].sort_key) < 0) {
                parsed[count - 1] = it;
            }
        }
    }
    qsort(parsed, (size_t)count, sizeof(parsed[0]), by_time);
    return count;
}

static void
publish(int count, int from_pi) {
    mutex_lock(&events_mutex);
    memcpy(items, parsed, sizeof(parsed[0]) * (size_t)count);
    status.count = count;
    status.from_pi = from_pi;
    status.generation++;
    status.state = EVENTS_DONE;
    snprintf(status.line, sizeof(status.line), "%s", from_pi ? "From the DreamPi" : "From dc99.net");
    mutex_unlock(&events_mutex);
}

static void
run_fetch(void) {
    struct sockaddr_in addr;
    char host[20];
    size_t used;
    int code, count;

    /* the DreamPi add-on first (online through the modem and the add-on answers) */
    if (dreampi_link_pi(&addr, host, sizeof(host), &abort_requested)) {
        set_line(EVENTS_RUNNING, "Asking the DreamPi...");
        code = http_get(&addr, host, PI_PATH, PI_TIMEOUT_MS, &used);
        if (code == -2) {
            set_line(EVENTS_IDLE, "");
            return;
        }
        const char* json = code == 200 ? strstr(body, "\r\n\r\n") : NULL;
        count = json ? parse_events(json + 4, "\"events\"", 1) : -1;
        if (count >= 0) {
            publish(count, 1);
            return;
        }
    }
    if (abort_requested) {
        set_line(EVENTS_IDLE, "");
        return;
    }
    /* then the community page itself */
    set_line(EVENTS_RUNNING, "Reading dc99.net...");
    if (!dcnow_resolve(DC99_HOST, 80, &addr)) {
        set_line(EVENTS_FAILED, "dc99.net could not be found");
        return;
    }
    code = http_get(&addr, DC99_HOST, DC99_PATH, WEB_TIMEOUT_MS, &used);
    if (code == -2) {
        set_line(EVENTS_IDLE, "");
        return;
    }
    if (code != 200) {
        set_line(EVENTS_FAILED, code >= 300 && code < 400 ? "dc99.net wants https (no events)" : "dc99.net did not answer");
        return;
    }
    count = parse_events(body, "const EVENTS", 0);
    if (count < 0) {
        set_line(EVENTS_FAILED, "No event list on dc99.net");
        return;
    }
    publish(count, 0);
}

static void*
events_main(void* param) {
    (void)param;
    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4); /* below the menu: a connection or a fetch never takes the frame's time */
    run_fetch();
    worker_done = 1;
    return NULL;
}

static void
join_worker(void) {
    if (worker != NULL) {
        thd_join(worker, NULL);
        worker = NULL;
    }
}

void
events_fetch_start(void) {
    if (worker != NULL && !worker_done) {
        restart_pending = abort_requested; /* an aborted fetch is still ending (it never blocks the menu): start when it has */
        return;
    }
    restart_pending = 0;
    join_worker();
    abort_requested = 0;
    worker_done = 0;
    set_line(EVENTS_RUNNING, "Fetching events...");
    worker = thd_create(0, events_main, NULL);
    if (worker == NULL) {
        set_line(EVENTS_FAILED, "Could not start the fetch");
    }
}

void
events_fetch_abort(void) {
    /* Only asks the worker to stop: waiting for it here froze the menu when it was inside a lookup or a connection. It is joined
     * by the poll once it has ended. */
    abort_requested = 1;
    restart_pending = 0;
    mutex_lock(&events_mutex);
    if (status.state == EVENTS_RUNNING) {
        status.state = EVENTS_IDLE;
    }
    mutex_unlock(&events_mutex);
}

void
events_fetch_poll(events_status_t* out) {
    if (worker != NULL && worker_done) {
        join_worker();
        if (restart_pending) {
            restart_pending = 0;
            events_fetch_start();
        }
    }
    mutex_lock(&events_mutex);
    *out = status;
    mutex_unlock(&events_mutex);
}

int
events_fetch_copy(event_item_t* out, int max) {
    mutex_lock(&events_mutex);
    int n = status.count < max ? status.count : max;
    memcpy(out, items, sizeof(items[0]) * (size_t)n);
    mutex_unlock(&events_mutex);
    return n;
}
