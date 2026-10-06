/*
 * File: dreampi_link.c
 * Project: openmenu
 * Talks to the DreamPi Netswitch add-on over the PPP link. The Pi is the
 * Dreamcast's DNS server, so its address is already known. Everything here is
 * the Dreamcast asking: the Pi never pushes anything.
 *
 *   GET  /openmenu/poll?v=1&n=<games>&h=<hash>   answers "openmenu 1", then
 *        "NEED games" when the Pi lacks this list, and "LAUNCH <product>"
 *        when someone picked a game on the phone page.
 *   POST /openmenu/games                         the game list, one game per line.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/net.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <openmenu_settings.h>
#include <sys/socket.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>

#include "backend/dcnow_net.h"
#include "backend/dreampi_link.h"
#include "ui/draw_prototypes.h"

#define LINK_PORT          80
#define LINK_POLL_MS       3000
#define LINK_IO_TIMEOUT_MS 8000
#define LINK_MAX_FAILS     5  /* in a row, once the Pi has answered */
#define LINK_FIRST_FAILS   12 /* before the Pi has answered: the link has just come up and may not carry traffic yet */
#define LINK_FIRST_WAIT_MS 5000
#define LINK_REPLY_MAX     1024 /* the Pi's answer: its headers are about 450 bytes, then the lines */
#define LINK_LINE_MAX      400
#define PLAYING_MAX        32
#define LINK_PRODUCT_MAX   12

static mutex_t link_mutex = MUTEX_INITIALIZER;
static kthread_t* worker = NULL;
static volatile int worker_done = 0;
static volatile int link_up = 0;       /* set by the main thread: a modem connection is online */
static volatile int stop_requested = 0;
static volatile int ui_is_idle = 0;
static volatile int pi_state = 0;      /* 0 not known yet, 1 the add-on answered, 2 there is none on this link */
static int tried_this_link = 0;        /* main thread only: one worker per connection */
static char pending[LINK_PRODUCT_MAX]; /* guarded by link_mutex */

/* The live info in the Pi's answers (guarded by link_mutex): the selected network, and which games of the card someone plays online now
 * (slot numbers, or product codes). playing_valid says the Pi answered on this connection, so an empty list means nobody. */
static int net_kind = 0; /* 0 not known, 1 DCNow!, 2 DCNET */
static char playing_tok[PLAYING_MAX][LINK_PRODUCT_MAX];
static int playing_n[PLAYING_MAX]; /* players in that game */
static int playing_count = 0;
static volatile int playing_valid = 0;

/* The next DC99 event, from the Pi's "EVN <minutes> <flag> <source> <title>" line: minutes until it starts (negative once it has), flag 1 while
 * a reminder is due, the source as one word (discord, dreamcastlive, manual ...). The banner shows for 20 seconds when a reminder first
 * comes due and again at 5 minutes, at 1 minute and at the start. */
#define EVENT_TITLE_MAX  48
#define EVENT_SOURCE_MAX 16
#define EVENT_SHOW_MS    20000
static char event_title[EVENT_TITLE_MAX];
static char event_source[EVENT_SOURCE_MAX];
static int event_minutes = 0;
static int event_flag = 0;
static volatile int event_valid = 0;
static uint64_t event_stamp = 0;      /* timer_ms_gettime64() when the minutes were told */
static uint64_t event_show_until = 0; /* main thread only */
static uint32_t event_seen_hash = 0;
static int event_seen_bucket = -1;

/* Waits for a socket event in 100 ms slices. Returns the events, 0 on timeout, -1 when asked to stop. */
static int
wait_socket(int fd, short events, uint64_t deadline) {
    struct pollfd pfd;

    while (timer_ms_gettime64() < deadline) {
        if (stop_requested) {
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

static int
send_all(int fd, const char* data, size_t len, uint64_t deadline) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);

        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            int rc = wait_socket(fd, POLLWRNORM, deadline);

            if (rc > 0 && !(rc & (POLLHUP | POLLERR))) {
                continue;
            }
        }
        return 0;
    }
    return 1;
}

/* The Pi's end of the PPP link is the DNS server it handed out. */
static int
pi_address(struct sockaddr_in* out, char* text, size_t text_len) {
    const uint8_t* d;

    if (net_default_dev == NULL) {
        return 0;
    }
    d = net_default_dev->dns;
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(LINK_PORT);
    memcpy(&out->sin_addr, d, 4);
    snprintf(text, text_len, "%u.%u.%u.%u", d[0], d[1], d[2], d[3]);
    return 1;
}

typedef int (*body_fn)(int fd, uint64_t deadline, void* ctx);

/* One request. Returns the HTTP status, or -1 when it failed. The answer's body
 * is copied to reply (terminated, cut to reply_max). */
static int
http_exchange(const struct sockaddr_in* addr, const char* head, body_fn body, void* ctx, char* reply,
              size_t reply_max) {
    char buf[LINK_REPLY_MAX + 512];
    size_t used = 0;
    uint64_t deadline = timer_ms_gettime64() + LINK_IO_TIMEOUT_MS;
    int status = -1;
    char* split;
    int fd;
    int rc;

    reply[0] = '\0';
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
        return -1;
    }
    if (!send_all(fd, head, strlen(head), deadline) || (body != NULL && !body(fd, deadline, ctx))) {
        close(fd);
        return -1;
    }
    for (;;) {
        ssize_t got;

        rc = wait_socket(fd, POLLRDNORM, deadline);
        if (rc <= 0) {
            close(fd);
            return -1;
        }
        got = recv(fd, buf + used, sizeof(buf) - 1 - used, 0);
        if (got > 0) {
            used += (size_t)got;
            if (used >= sizeof(buf) - 1) {
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
    buf[used] = '\0';
    if (sscanf(buf, "HTTP/%*d.%*d %d", &status) != 1) {
        return -1;
    }
    split = strstr(buf, "\r\n\r\n");
    if (split != NULL) {
        snprintf(reply, reply_max, "%s", split + 4);
    }
    return status;
}

/* One line per game. Control characters are blanked so a name cannot break the format. */
static int
format_game(const gd_item* item, char* out, size_t out_len) {
    char name[sizeof(item->name)];
    char folder[sizeof(item->folder)];
    int n;

    for (size_t i = 0; i < sizeof(name); i++) {
        name[i] = (unsigned char)item->name[i] < 0x20 ? ' ' : item->name[i];
    }
    name[sizeof(name) - 1] = '\0';
    for (size_t i = 0; i < sizeof(folder); i++) {
        folder[i] = (unsigned char)item->folder[i] < 0x20 ? ' ' : item->folder[i];
    }
    folder[sizeof(folder) - 1] = '\0';
    n = snprintf(out, out_len, "%s\t%u\t%s\t%s\t%s\t%s\n", item->product, item->slot_num, item->disc, item->region,
                 folder, name);
    return n < (int)out_len ? n : (int)out_len - 1;
}

static int
game_listed(const gd_item* item) {
    return item != NULL && item->product[0] != '\0' && strcmp(item->disc, "DIR") != 0;
}

/* Counts the games and folds them into one number, so the Pi can tell whether it already has this list. */
static unsigned int
games_hash(int* count) {
    unsigned int h = 2166136261u;
    int n = 0;

    for (int i = 1; i < list_all_count(); i++) {
        const gd_item* item = list_all_item(i);

        if (!game_listed(item)) {
            continue;
        }
        h = (h ^ gd_item_recent_hash(item)) * 16777619u;
        n++;
    }
    *count = n;
    return h;
}

typedef struct upload_ctx {
    char header[64];
} upload_ctx_t;

static int
send_games(int fd, uint64_t deadline, void* ctx) {
    upload_ctx_t* up = ctx;
    char line[1024];
    char chunk[1400];
    size_t used = 0;

    if (!send_all(fd, up->header, strlen(up->header), deadline)) {
        return 0;
    }
    for (int i = 1; i < list_all_count(); i++) {
        const gd_item* item = list_all_item(i);
        int n;

        if (!game_listed(item)) {
            continue;
        }
        n = format_game(item, line, sizeof(line));
        if (used + (size_t)n > sizeof(chunk)) {
            if (!send_all(fd, chunk, used, deadline)) {
                return 0;
            }
            used = 0;
        }
        memcpy(chunk + used, line, (size_t)n);
        used += (size_t)n;
    }
    return used == 0 || send_all(fd, chunk, used, deadline);
}

static int
upload_games(const struct sockaddr_in* addr, const char* host, unsigned int hash, int count) {
    upload_ctx_t up;
    char head[256];
    char reply[LINK_REPLY_MAX];
    char line[1024];
    size_t body_len;

    snprintf(up.header, sizeof(up.header), "#openmenu-games 1 %08x %d\n", hash, count);
    body_len = strlen(up.header);
    for (int i = 1; i < list_all_count(); i++) {
        const gd_item* item = list_all_item(i);

        if (game_listed(item)) {
            body_len += (size_t)format_game(item, line, sizeof(line));
        }
    }
    snprintf(head, sizeof(head),
                 "POST /openmenu/games HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\nX-Requested-With: openMenu\r\n"
                 "Content-Type: text/plain; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                 host, (unsigned int)body_len);
    return http_exchange(addr, head, send_games, &up, reply, sizeof(reply)) == 200;
}

static int
product_valid(const char* text) {
    size_t len = strlen(text);

    if (len == 0 || len >= LINK_PRODUCT_MAX) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)text[i]) && text[i] != '-' && text[i] != '_') {
            return 0;
        }
    }
    return 1;
}

/* Reads the Pi's answer to a poll. Returns 1 when it is an openMenu answer. Lines: "openmenu 1", "NEED games", "LAUNCH <product>", "NET
 * dcnow|dcnet", "PLY <slot>:<players> ..." (a slot number or a product code, and
 * how many play that game; "PLAYING <slot> ..." from an older add-on counts one each). */
static int
read_poll_reply(const char* reply, int* need_games) {
    const char* p = reply;
    char tokens[PLAYING_MAX][LINK_PRODUCT_MAX];
    int counts[PLAYING_MAX];
    int token_count = 0;
    char ev_t[EVENT_TITLE_MAX] = "";
    char ev_s[EVENT_SOURCE_MAX] = "";
    int ev_m = 0;
    int ev_f = 0;
    int ev_seen = 0;
    int net = 0;
    int seen = 0;

    *need_games = 0;
    while (*p) {
        char line[LINK_LINE_MAX];
        size_t len = strcspn(p, "\r\n");

        snprintf(line, sizeof(line), "%.*s", (int)len, p);
        if (!strncmp(line, "openmenu ", 9)) {
            seen = 1;
        } else if (!strcmp(line, "NEED games")) {
            *need_games = 1;
        } else if (!strncmp(line, "LAUNCH ", 7) && product_valid(line + 7)) {
            mutex_lock(&link_mutex);
            snprintf(pending, sizeof(pending), "%s", line + 7);
            mutex_unlock(&link_mutex);
        } else if (!strcmp(line, "NET dcnow")) {
            net = 1;
        } else if (!strcmp(line, "NET dcnet")) {
            net = 2;
        } else if (!strncmp(line, "EVN ", 4)) {
            int minutes, flag, used = 0;
            char source[EVENT_SOURCE_MAX];

            /* Letters, digits and punctuation only: the menu's font stops at ASCII. */
            if (sscanf(line + 4, "%d %d %15s %n", &minutes, &flag, source, &used) >= 3 && used > 0) {
                size_t n = 0;

                for (const char* t = line + 4 + used; *t != '\0' && n < sizeof(ev_t) - 1; t++) {
                    if ((unsigned char)*t >= 0x20 && (unsigned char)*t < 0x7F) {
                        ev_t[n++] = *t;
                    }
                }
                ev_t[n] = '\0';
                snprintf(ev_s, sizeof(ev_s), "%s", source);
                ev_m = minutes;
                ev_f = flag != 0;
                ev_seen = n > 0;
            }
        } else if (!strncmp(line, "PLY ", 4) || !strncmp(line, "PLAYING ", 8)) {
            char* tok = line + (line[2] == 'Y' ? 4 : 8);

            while (*tok != '\0' && token_count < PLAYING_MAX) {
                char* end = tok + strcspn(tok, " ");
                char saved = *end;
                char* colon;
                int players = 1;

                *end = '\0';
                colon = strchr(tok, ':');
                if (colon != NULL) {
                    *colon = '\0';
                    players = atoi(colon + 1);
                }
                if (product_valid(tok) && players > 0) {
                    snprintf(tokens[token_count], LINK_PRODUCT_MAX, "%s", tok);
                    counts[token_count++] = players;
                }
                if (saved == '\0') {
                    break;
                }
                tok = end + 1;
            }
        }
        p += len;
        p += strspn(p, "\r\n");
    }
    if (seen) {
        mutex_lock(&link_mutex);
        net_kind = net;
        playing_count = token_count;
        memcpy(playing_tok, tokens, sizeof(playing_tok[0]) * (size_t)token_count);
        memcpy(playing_n, counts, sizeof(playing_n[0]) * (size_t)token_count);
        playing_valid = 1;
        event_valid = ev_seen;
        if (ev_seen) {
            snprintf(event_title, sizeof(event_title), "%s", ev_t);
            snprintf(event_source, sizeof(event_source), "%s", ev_s);
            event_minutes = ev_m;
            event_flag = ev_f;
            event_stamp = timer_ms_gettime64();
        }
        mutex_unlock(&link_mutex);
    }
    return seen;
}

static void*
link_main(void* param) {
    struct sockaddr_in addr;
    char host[20];
    char head[200];
    char reply[LINK_REPLY_MAX];
    int fails = 0;
    int seen_pi = 0;

    (void)param;
    while (link_up && !stop_requested) {
        unsigned int hash;
        int count;
        int need_games = 0;
        int status;

        if (!pi_address(&addr, host, sizeof(host))) {
            break;
        }
        hash = games_hash(&count);
        snprintf(head, sizeof(head),
                 "GET /openmenu/poll?v=1&n=%d&h=%08x HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\n"
                 "Connection: close\r\n\r\n",
                 count, hash, host);
        status = http_exchange(&addr, head, NULL, NULL, reply, sizeof(reply));
        if (status == 200 && read_poll_reply(reply, &need_games)) {
            seen_pi = 1;
            pi_state = 1;
            fails = 0;
            if (need_games && count > 0 && !stop_requested) {
                upload_games(&addr, host, hash, count);
            }
        } else if (++fails >= (seen_pi ? LINK_MAX_FAILS : LINK_FIRST_FAILS)) {
            break; /* not a DreamPi with the add-on, or it went away */
        }
        for (int waited = 0; waited < LINK_POLL_MS && link_up && !stop_requested; waited += 100) {
            thd_sleep(100);
        }
    }
    pi_state = 2; /* nothing more will be answered on this connection */
    worker_done = 1;
    return NULL;
}

static void dreampi_link_forget(void);

static void
join_worker(void) {
    if (worker != NULL) {
        thd_join(worker, NULL);
        worker = NULL;
    }
}

/* The live info is only as good as the connection that brought it. */
static void
dreampi_link_forget(void) {
    if (!playing_valid && net_kind == 0 && !event_valid) {
        return;
    }
    mutex_lock(&link_mutex);
    net_kind = 0;
    playing_count = 0;
    playing_valid = 0;
    event_valid = 0;
    mutex_unlock(&link_mutex);
}

int
dreampi_link_network(void) {
    return net_kind;
}

int
dreampi_link_playing_known(void) {
    return playing_valid;
}

int
dreampi_link_game_playing(const gd_item* item) {
    int players = 0;

    if (!playing_valid || item == NULL) {
        return 0;
    }
    mutex_lock(&link_mutex);
    for (int i = 0; i < playing_count && players == 0; i++) {
        const char* tok = playing_tok[i];
        const int numeric = isdigit((unsigned char)tok[0]) && strspn(tok, "0123456789") == strlen(tok);

        /* A slot number is the game's place on the card (its SD folder); anything else is a product code. */
        if (numeric ? (unsigned int)atoi(tok) == item->slot_num : (item->product[0] != '\0' && !strcmp(tok, item->product))) {
            players = playing_n[i];
        }
    }
    mutex_unlock(&link_mutex);
    return players;
}

static const char*
event_source_label(const char* source) {
    if (!strcasecmp(source, "discord")) {
        return "Sega Discord";
    }
    if (!strcasecmp(source, "dreamcastlive") || !strcasecmp(source, "dclive")) {
        return "Dreamcast Live";
    }
    if (!strcasecmp(source, "manual") || !strcasecmp(source, "dc99")) {
        return "DC99";
    }
    return source;
}

/* Called every frame from the main loop's tick (main thread). */
static void
event_tick(void) {
    const uint64_t now = timer_ms_gettime64();
    int minutes;
    int bucket;
    uint32_t hash = 2166136261u;

    if (!event_valid || !event_flag) {
        event_seen_hash = 0;
        event_seen_bucket = -1;
        return;
    }
    mutex_lock(&link_mutex);
    minutes = event_minutes - (int)((now - event_stamp) / 60000);
    for (const char* t = event_title; *t != '\0'; t++) {
        hash = (hash ^ (unsigned char)*t) * 16777619u;
    }
    mutex_unlock(&link_mutex);
    bucket = minutes > 5 ? 3 : (minutes > 1 ? 2 : (minutes > 0 ? 1 : 0));
    if (hash != event_seen_hash || bucket != event_seen_bucket) {
        event_seen_hash = hash;
        event_seen_bucket = bucket;
        event_show_until = now + EVENT_SHOW_MS;
    }
}

int
dreampi_link_event_banner(char* line1, size_t line1_len, char* line2, size_t line2_len) {
    const uint64_t now = timer_ms_gettime64();
    int minutes;

    if (!event_valid || !event_flag || now >= event_show_until) {
        return 0;
    }
    mutex_lock(&link_mutex);
    minutes = event_minutes - (int)((now - event_stamp) / 60000);
    snprintf(line2, line2_len, "%s", event_title);
    snprintf(line1, line1_len, "%s", event_source_label(event_source));
    mutex_unlock(&link_mutex);
    {
        const size_t used = strlen(line1);

        if (minutes > 1) {
            snprintf(line1 + used, line1_len - used, ": in %d min", minutes);
        } else if (minutes == 1) {
            snprintf(line1 + used, line1_len - used, ": in 1 min");
        } else if (minutes == 0) {
            snprintf(line1 + used, line1_len - used, ": starting now");
        } else {
            snprintf(line1 + used, line1_len - used, ": started %d min ago", -minutes);
        }
    }
    return 1;
}

void
dreampi_link_abort(void) {
    stop_requested = 1;
    join_worker();
    stop_requested = 0;
    worker_done = 0;
    pi_state = 0;
    dreampi_link_forget();
}

void
dreampi_link_ui_idle(int idle) {
    ui_is_idle = idle;
}

int
dreampi_link_pi(struct sockaddr_in* addr, char* host, size_t host_len, const volatile int* abort_flag) {
    uint64_t deadline = timer_ms_gettime64() + LINK_FIRST_WAIT_MS;

    while (link_up && pi_state == 0 && timer_ms_gettime64() < deadline && !(abort_flag != NULL && *abort_flag)) {
        thd_sleep(100);
    }
    return link_up && pi_state == 1 && pi_address(addr, host, host_len);
}

void
dreampi_link_tick(void) {
    dcnow_status_t snap;
    char product[LINK_PRODUCT_MAX] = "";
    int modem_online = 0;

    if (sf_dcnow[0] != DCNOW_OFF) {
        dcnow_conn_poll(&snap);
        modem_online = snap.state == DCNOW_CONN_ONLINE && dcnow_device_hint() == DCNOW_DEV_MODEM;
    }
    link_up = modem_online;
    event_tick();

    if (worker != NULL && worker_done) {
        join_worker();
        worker_done = 0;
    }
    if (!modem_online) {
        tried_this_link = 0;
        pi_state = 0;
        dreampi_link_forget();
    } else if (worker == NULL && !tried_this_link) {
        tried_this_link = 1;
        worker = thd_create(false, link_main, NULL);
    }

    if (!ui_is_idle) {
        return;
    }
    mutex_lock(&link_mutex);
    if (pending[0]) {
        snprintf(product, sizeof(product), "%s", pending);
        pending[0] = '\0';
    }
    mutex_unlock(&link_mutex);
    if (product[0]) {
        const gd_item* item = list_find_by_product(product);

        if (item != NULL) {
            dreamcast_launch_disc(item);
        }
    }
}
