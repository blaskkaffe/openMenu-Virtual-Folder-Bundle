/*
 * File: dcnow_fetch.c
 * Project: openmenu
 * Dreamcast Now! player list: HTTP fetch and JSON scan on a worker thread.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <arch/rtc.h>
#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "backend/dcnow_fetch.h"

#define DCNOW_HOST            "dreamcast.online"
#define DCNOW_PATH            "/now/api/users.json"
/* dc99.net's combined DreamPi/DCNet status feed. Confirmed against a live
 * response: {"generated":..,"dreampi":{"users":[{"username","country",
 * "current_game_display","current_game","online",...},...]},"dcnet":{
 * "online":bool,"error":..,"games":[...],"players":[{"name","gameId",
 * "gameName","geoloc":{"country",...}},...],"users":[...]}}. dreampi.users
 * is the same shape as dreamcast.online's own feed (DC Now); dcnet.players
 * is DCNet's online players, nested one level deeper for country. Both are
 * combined into one list below. The response carries more than we need (a
 * full per-user game history under dreampi, dcnet.games, dcnet.users), so
 * it's larger than dreamcast.online's own feed; DCNOW_BODY_MAX has room for
 * the sample this was checked against, but a very busy DreamPi could outgrow
 * it, in which case parse_dc99_combined() reports this fetch as failed
 * rather than showing a wrong list - dreamcast.online's own feed, fetched
 * separately, is unaffected either way. */
#define DC99_HOST             "dc99.net"
#define DC99_PATH             "/online/dcnet_status.php"
#define DCNOW_BODY_MAX        98304
#define DCNOW_STEP_TIMEOUT_MS 10000
#define DCNOW_GET_TIMEOUT_MS  60000

static mutex_t fetch_mutex = MUTEX_INITIALIZER;
static dcnow_fetch_status_t fetch = {DCNOW_FETCH_IDLE, -1, 0, {{0}}, 0, 0, 0, 0, 0, 0};
static dcnow_player_t players[DCNOW_PLAYER_MAX];
static kthread_t* fetch_worker = NULL;
static volatile int abort_requested = 0;

/* The worker's own buffers. */
static char body[DCNOW_BODY_MAX + 1];
static dcnow_player_t parsed[DCNOW_PLAYER_MAX];

static void
fetch_reset(dcnow_fetch_state_t state) {
    mutex_lock(&fetch_mutex);
    fetch.state = state;
    fetch.counter_line = -1;
    fetch.count = 0;
    mutex_unlock(&fetch_mutex);
}

/* Appends a line, dropping the oldest when the ring is full. */
static void
fetch_push(const char* text, int with_counter) {
    mutex_lock(&fetch_mutex);
    if (fetch.count == DCNOW_STATUS_LINES) {
        memmove(fetch.lines[0], fetch.lines[1], sizeof(fetch.lines[0]) * (DCNOW_STATUS_LINES - 1));
        fetch.count--;
        if (fetch.counter_line == 0) {
            fetch.counter_line = -1;
        } else if (fetch.counter_line > 0) {
            fetch.counter_line--;
        }
    }
    snprintf(fetch.lines[fetch.count], DCNOW_STATUS_WIDTH, "%s", text);
    if (with_counter) {
        fetch.counter_line = fetch.count;
        fetch.counter_since = timer_ms_gettime64();
    }
    fetch.count++;
    mutex_unlock(&fetch_mutex);
}

/* Rewrites one line in place and stops its counter. */
static void
fetch_replace(int index, const char* text) {
    mutex_lock(&fetch_mutex);
    if (index >= 0 && index < fetch.count) {
        snprintf(fetch.lines[index], DCNOW_STATUS_WIDTH, "%s", text);
        if (fetch.counter_line == index) {
            fetch.counter_line = -1;
        }
    }
    mutex_unlock(&fetch_mutex);
}

static int
fetch_last_index(void) {
    int index;
    mutex_lock(&fetch_mutex);
    index = fetch.count - 1;
    mutex_unlock(&fetch_mutex);
    return index;
}

/* Marks the fetch failed and adds the two closing lines below the step lines
 * already on screen. */
static void
fetch_fail(const char* reason) {
    mutex_lock(&fetch_mutex);
    fetch.state = DCNOW_FETCH_FAILED;
    fetch.counter_line = -1;
    mutex_unlock(&fetch_mutex);
    fetch_push("Connection failed.", 0);
    fetch_push(reason, 0);
}

/* Waits for a socket event in 100 ms slices. Returns the events, 0 when the
 * deadline passed, -1 when the fetch was aborted. */
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

/* JSON helpers. The feed is small and well formed, so this is a scanner, not
 * a validator: it reads what it needs and skips everything else. */

static const char*
json_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/* Copies a JSON string into out, decoding the simple escapes and dropping
 * anything outside ASCII. Returns the position after the closing quote, or
 * NULL when the text ends first. */
static const char*
json_string(const char* p, char* out, size_t out_len) {
    size_t n = 0;
    int dropped = 0;

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
                    code = code * 16
                           + (unsigned int)(isdigit((unsigned char)p[i]) ? p[i] - '0'
                                                                         : (tolower((unsigned char)p[i]) - 'a' + 10));
                }
                p += 4;
                if (code >= 0x80) {
                    dropped = 1;
                    continue;
                }
                c = (char)code;
            } else if (e == 'n' || e == 'r' || e == 't' || e == 'b' || e == 'f') {
                c = ' ';
            } else {
                c = e;
            }
        } else if ((unsigned char)c >= 0x80) {
            /* The fonts stop at ASCII, so a multi-byte UTF-8 sequence goes
             * the same way as an escape above 0x7F. */
            while (((unsigned char)*p & 0xC0) == 0x80) {
                p++;
            }
            dropped = 1;
            continue;
        }
        if (out != NULL && n < out_len - 1) {
            out[n++] = c;
        }
    }
    if (out != NULL) {
        /* A string made only of such characters still has to show as something. */
        if (n == 0 && dropped && out_len > 1) {
            out[n++] = '?';
        }
        out[n] = '\0';
    }
    return *p == '"' ? p + 1 : NULL;
}

/* Skips one value of any kind, nesting included. Returns the position after
 * it, or NULL when the text ends first. */
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

/* Reads one entry of the users array into out. Returns the position after
 * the object, or NULL on malformed text. *online tells whether to keep it. */
static const char*
json_user(const char* p, dcnow_player_t* out, int* online) {
    char key[32];

    memset(out, 0, sizeof(*out));
    out->network = DCNOW_NET_DCNOW;
    *online = 0;
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
        if (strcmp(key, "username") == 0 && *p == '"') {
            p = json_string(p, out->name, sizeof(out->name));
        } else if (strcmp(key, "country") == 0 && *p == '"') {
            p = json_string(p, out->country, sizeof(out->country));
        } else if (strcmp(key, "current_game_display") == 0 && *p == '"') {
            p = json_string(p, out->title, sizeof(out->title));
        } else if (strcmp(key, "current_game") == 0 && *p == '"') {
            p = json_string(p, out->code, sizeof(out->code));
        } else if (strcmp(key, "online") == 0) {
            *online = strncmp(p, "true", 4) == 0;
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

/* Fills parsed[] with the online users, up to the store's size, and counts
 * every online user in *total. Returns the stored count, or -1 when the text
 * is not the feed we expect. A body cut at the cap ends inside a record, so
 * with truncated set the players already read still count. */
static int
parse_users(const char* text, int truncated, int* total) {
    const char* p = strstr(text, "\"users\"");
    int count = 0;

    *total = 0;

    if (p == NULL) {
        return -1;
    }
    p = json_ws(p + 7);
    if (*p != ':') {
        return -1;
    }
    p = json_ws(p + 1);
    if (*p != '[') {
        return -1;
    }
    p = json_ws(p + 1);
    while (*p != ']') {
        dcnow_player_t entry;
        int online;

        p = json_user(p, &entry, &online);
        if (p == NULL) {
            return truncated && count > 0 ? count : -1;
        }
        if (online && entry.name[0] != '\0') {
            (*total)++;
            if (count < DCNOW_PLAYER_MAX) {
                parsed[count++] = entry;
            }
        }
        p = json_ws(p);
        if (*p == ',') {
            p = json_ws(p + 1);
        } else if (*p != ']') {
            return truncated && count > 0 ? count : -1;
        }
    }
    return count;
}

/* Reads {"country":"XX",...} (dcnet.players[].geoloc) into out, keeping only
 * "country". Returns the position after the object, or NULL on malformed
 * text. */
static const char*
json_geoloc_country(const char* p, char* out, size_t out_len) {
    char key[32];

    if (out_len > 0) {
        out[0] = '\0';
    }
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
        if (strcmp(key, "country") == 0 && *p == '"') {
            p = json_string(p, out, out_len);
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

/* Reads one entry of dcnet.players into out: {"geoloc":{...},"name":..,
 * "gameId":..,"gameName":..,"thumbnail":..}. Returns the position after the
 * object, or NULL on malformed text. Every entry in this array is an online
 * DCNet player; unlike dreamcast.online's feed there is no per-entry "online"
 * flag to check. */
static const char*
json_dcnet_player(const char* p, dcnow_player_t* out) {
    char key[32];

    memset(out, 0, sizeof(*out));
    out->network = DCNOW_NET_DCNET;
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
        if (strcmp(key, "name") == 0 && *p == '"') {
            p = json_string(p, out->name, sizeof(out->name));
        } else if (strcmp(key, "gameName") == 0 && *p == '"') {
            p = json_string(p, out->title, sizeof(out->title));
        } else if (strcmp(key, "gameId") == 0 && *p == '"') {
            p = json_string(p, out->code, sizeof(out->code));
        } else if (strcmp(key, "geoloc") == 0 && *p == '{') {
            p = json_geoloc_country(p, out->country, sizeof(out->country));
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

/* Appends dc99.net's dreampi.users (online-filtered, DC Now) into parsed[]
 * starting at *count, counting every online entry not already in
 * parsed[0..known_dcnow_count) in *total. known_dcnow_count is how many DC
 * Now players dreamcast.online's own feed already contributed to parsed[]
 * before this ran (0 if that fetch failed or found none): dc99.net mirrors
 * the same DC Now presence, so a name already there is the same player
 * heard twice, not a second one. Returns 1 when the section was found and
 * fully read (even if it added zero players), 0 when the section is simply
 * missing (not an error - the dcnet section can still stand alone), or -1
 * when it was found but malformed. */
static int
append_dc99_dreampi(const char* text, int truncated, int* count, int* total, int known_dcnow_count) {
    const char* dreampi = strstr(text, "\"dreampi\"");
    const char* p;

    if (dreampi == NULL) {
        return 0;
    }
    p = strstr(dreampi, "\"users\"");
    if (p == NULL) {
        return -1;
    }
    p = json_ws(p + 7);
    if (*p != ':') {
        return -1;
    }
    p = json_ws(p + 1);
    if (*p != '[') {
        return -1;
    }
    p = json_ws(p + 1);
    while (*p != ']') {
        dcnow_player_t entry;
        int online;
        int dup = 0;

        p = json_user(p, &entry, &online);
        if (p == NULL) {
            return truncated && *count > 0 ? 1 : -1;
        }
        if (online && entry.name[0] != '\0') {
            for (int i = 0; i < known_dcnow_count; i++) {
                if (strcasecmp(parsed[i].name, entry.name) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                (*total)++;
                if (*count < DCNOW_PLAYER_MAX) {
                    parsed[(*count)++] = entry;
                }
            }
        }
        p = json_ws(p);
        if (*p == ',') {
            p = json_ws(p + 1);
        } else if (*p != ']') {
            return truncated && *count > 0 ? 1 : -1;
        }
    }
    return 1;
}

/* Appends dc99.net's dcnet.players (DCNet) into parsed[] starting at *count,
 * counting every entry in *total. Same return convention as
 * append_dc99_dreampi(). dcnet.online is read but not treated as a failure
 * either way: an offline DCNet simply has no players to add, and the dreampi
 * section can still stand alone. */
static int
append_dc99_dcnet(const char* text, int truncated, int* count, int* total) {
    const char* dcnet = strstr(text, "\"dcnet\"");
    const char* online;
    const char* p;

    if (dcnet == NULL) {
        return 0;
    }
    online = strstr(dcnet, "\"online\"");
    if (online != NULL) {
        const char* v = json_ws(online + 8);
        if (*v == ':') {
            v = json_ws(v + 1);
            if (strncmp(v, "false", 5) == 0) {
                return 1;
            }
        }
    }
    p = strstr(dcnet, "\"players\"");
    if (p == NULL) {
        return -1;
    }
    p = json_ws(p + 9);
    if (*p != ':') {
        return -1;
    }
    p = json_ws(p + 1);
    if (*p != '[') {
        return -1;
    }
    p = json_ws(p + 1);
    while (*p != ']') {
        dcnow_player_t entry;

        p = json_dcnet_player(p, &entry);
        if (p == NULL) {
            return truncated && *count > 0 ? 1 : -1;
        }
        if (entry.name[0] != '\0') {
            (*total)++;
            if (*count < DCNOW_PLAYER_MAX) {
                parsed[(*count)++] = entry;
            }
        }
        p = json_ws(p);
        if (*p == ',') {
            p = json_ws(p + 1);
        } else if (*p != ']') {
            return truncated && *count > 0 ? 1 : -1;
        }
    }
    return 1;
}

/* Appends dc99.net's dreampi.users (DC Now, deduped against the
 * known_dcnow_count players dreamcast.online's own feed already contributed
 * to parsed[]) and dcnet.players (DCNet) into parsed[]/*count/*total, which
 * carry over whatever an earlier dreamcast.online fetch already put there.
 * Returns 0 when at least one section was read (even if it added zero
 * players), or -1 when neither section was found at all (the feed isn't the
 * shape we expect) or a section that was found turned out malformed. */
static int
parse_dc99_combined(const char* text, int truncated, int* count, int* total, int known_dcnow_count) {
    int dreampi_rc = append_dc99_dreampi(text, truncated, count, total, known_dcnow_count);
    int dcnet_rc = append_dc99_dcnet(text, truncated, count, total);

    if (dreampi_rc < 0 || dcnet_rc < 0) {
        return -1;
    }
    if (dreampi_rc == 0 && dcnet_rc == 0) {
        return -1;
    }
    return 0;
}

static void
publish(int count, int total) {
    mutex_lock(&fetch_mutex);
    memcpy(players, parsed, sizeof(players[0]) * (size_t)count);
    fetch.player_count = count;
    fetch.online_total = total;
    fetch.updated = rtc_unix_secs();
    fetch.list_valid = 1;
    fetch.generation++;
    fetch.state = DCNOW_FETCH_DONE;
    fetch.counter_line = -1;
    fetch.count = 0;
    mutex_unlock(&fetch_mutex);
}

/* Fetches path from host into body[], pushing status lines for each step.
 * Returns the JSON payload (after the blank line separating headers from
 * body) on success, with *used_out set to how many bytes were read - needed
 * by the parser's own truncated check. Returns NULL on any failure; the
 * step that failed is left as the last status line. The caller checks
 * abort_requested right after this returns to tell a genuine failure (which
 * it may still recover from if the other host answers) from an abort
 * (which it must not: everything stops at IDLE instead). */
static const char*
fetch_body(const char* host, const char* path, size_t* used_out) {
    struct sockaddr_in addr;
    char request[160];
    char text[DCNOW_STATUS_WIDTH];
    uint64_t deadline;
    size_t used = 0;
    size_t sent = 0;
    int line;
    int fd;
    int rc;
    int code;
    const char* json;

    snprintf(text, sizeof(text), "Fetching player list from %s...", host);
    fetch_push(text, 0);

    fetch_push("Resolving host...", 0);
    line = fetch_last_index();
    if (!dcnow_resolve(host, 80, &addr)) {
        fetch_replace(line, "Resolving host... Failed");
        return NULL;
    }
    fetch_replace(line, "Resolving host... OK");
    if (abort_requested) {
        return NULL;
    }

    fetch_push("Connecting to server...", 0);
    line = fetch_last_index();
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fetch_replace(line, "Connecting to server... Failed");
        return NULL;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != EAGAIN) {
        close(fd);
        fetch_replace(line, "Connecting to server... Failed");
        return NULL;
    }
    deadline = timer_ms_gettime64() + DCNOW_STEP_TIMEOUT_MS;
    rc = wait_socket(fd, POLLWRNORM, deadline);
    if (rc < 0) {
        close(fd);
        return NULL;
    }
    if (rc == 0 || (rc & (POLLHUP | POLLERR))) {
        close(fd);
        fetch_replace(line, "Connecting to server... Failed");
        return NULL;
    }
    fetch_replace(line, "Connecting to server... OK");

    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\nConnection: close\r\n\r\n", path, host);
    fetch_push("HTTP GET", 1);
    line = fetch_last_index();
    deadline = timer_ms_gettime64() + DCNOW_GET_TIMEOUT_MS;
    while (sent < strlen(request)) {
        ssize_t n = send(fd, request + sent, strlen(request) - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            rc = wait_socket(fd, POLLWRNORM, deadline);
            if (rc < 0) {
                close(fd);
                return NULL;
            }
            if (rc > 0) {
                continue;
            }
        }
        close(fd);
        fetch_replace(line, "HTTP GET failed after 60 seconds.");
        return NULL;
    }

    for (;;) {
        ssize_t got;

        rc = wait_socket(fd, POLLRDNORM, deadline);
        if (rc < 0) {
            close(fd);
            return NULL;
        }
        if (rc == 0) {
            close(fd);
            fetch_replace(line, "HTTP GET failed after 60 seconds.");
            return NULL;
        }
        got = recv(fd, body + used, DCNOW_BODY_MAX - used, 0);
        if (got > 0) {
            used += (size_t)got;
            if (used == DCNOW_BODY_MAX) {
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
    body[used] = '\0';

    if (sscanf(body, "HTTP/%*d.%*d %d", &code) != 1) {
        fetch_replace(line, "The server answer could not be read.");
        return NULL;
    }
    if (code != 200) {
        snprintf(text, sizeof(text), "The server answered with error %d.", code);
        fetch_replace(line, text);
        return NULL;
    }
    json = strstr(body, "\r\n\r\n");
    if (json == NULL) {
        fetch_replace(line, "The server answer could not be read.");
        return NULL;
    }
    *used_out = used;
    return json + 4;
}

/* Fetches both dreamcast.online's own feed and dc99.net's combined one, and
 * merges them: dc99.net's dreampi section mirrors the same DC Now presence
 * dreamcast.online reports directly, so a player already added from there
 * is skipped rather than listed twice (see append_dc99_dreampi()). Either
 * host failing on its own is not treated as a failure as long as the other
 * answered; only failing both is. */
static void
run_fetch(void) {
    size_t used1 = 0;
    size_t used2 = 0;
    const char* json1;
    const char* json2;
    int count = 0;
    int total = 0;
    int dcnow_ok = 0;
    int dc99_ok = 0;

    fetch_reset(DCNOW_FETCH_RUNNING);

    json1 = fetch_body(DCNOW_HOST, DCNOW_PATH, &used1);
    if (abort_requested) {
        fetch_reset(DCNOW_FETCH_IDLE);
        return;
    }
    if (json1 != NULL) {
        int rc = parse_users(json1, used1 == DCNOW_BODY_MAX, &total);
        if (rc >= 0) {
            count = rc;
            dcnow_ok = 1;
        }
    }
    dcnow_presence_lookup();

    json2 = fetch_body(DC99_HOST, DC99_PATH, &used2);
    if (abort_requested) {
        fetch_reset(DCNOW_FETCH_IDLE);
        return;
    }
    if (json2 != NULL && parse_dc99_combined(json2, used2 == DCNOW_BODY_MAX, &count, &total, count) == 0) {
        dc99_ok = 1;
    }

    if (!dcnow_ok && !dc99_ok) {
        fetch_fail("Check the network and try again.");
        return;
    }
    publish(count, total);
}

static void*
fetch_main(void* param) {
    (void)param;
    run_fetch();
    return NULL;
}

static void
join_worker(void) {
    if (fetch_worker != NULL) {
        thd_join(fetch_worker, NULL);
        fetch_worker = NULL;
    }
}

void
dcnow_fetch_start(void) {
    dcnow_fetch_status_t snap;

    dcnow_fetch_poll(&snap);
    if (snap.state == DCNOW_FETCH_RUNNING) {
        return;
    }
    join_worker();
    abort_requested = 0;
    fetch_reset(DCNOW_FETCH_RUNNING);
    fetch_worker = thd_create(false, fetch_main, NULL);
    if (fetch_worker == NULL) {
        fetch_fail("The server did not answer in time.");
    }
}

/* A resolver call in progress cannot be interrupted, so the join can take up
 * to two seconds while the worker is inside dcnow_resolve(). */
void
dcnow_fetch_abort(void) {
    abort_requested = 1;
    join_worker();
}

void
dcnow_fetch_clear(void) {
    dcnow_fetch_abort();
    mutex_lock(&fetch_mutex);
    fetch.state = DCNOW_FETCH_IDLE;
    fetch.counter_line = -1;
    fetch.count = 0;
    fetch.player_count = 0;
    fetch.online_total = 0;
    fetch.list_valid = 0;
    fetch.generation++;
    mutex_unlock(&fetch_mutex);
}

void
dcnow_fetch_poll(dcnow_fetch_status_t* out) {
    mutex_lock(&fetch_mutex);
    *out = fetch;
    mutex_unlock(&fetch_mutex);
}

int
dcnow_fetch_copy(dcnow_player_t* out, int max) {
    int count;

    mutex_lock(&fetch_mutex);
    count = fetch.player_count < max ? fetch.player_count : max;
    memcpy(out, players, sizeof(players[0]) * (size_t)count);
    mutex_unlock(&fetch_mutex);
    return count;
}
