/*
 * File: news_fetch.c
 * Project: openmenu
 * See news_fetch.h. The HTTP helpers follow events_fetch.c.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "backend/dcnow_net.h"
#include "backend/news_fetch.h"

#define BODY_MAX       (96 * 1024)
#define WEB_TIMEOUT_MS 30000
#define PER_SOURCE     12

static const struct {
    const char* host;
    const char* path;
    const char* label;
} sources[] = {{"dc.dreamcastlive.net", "/news.html", "DreamcastLive"}, {"dc.segaonline.net", "/", "Sega Online"}};
#define SOURCES ((int)(sizeof(sources) / sizeof(sources[0])))

static mutex_t news_mutex = MUTEX_INITIALIZER;
static news_status_t status;
static news_item_t items[NEWS_MAX];
static news_item_t parsed[NEWS_MAX];
static kthread_t* worker = NULL;
static volatile int abort_requested = 0;
static volatile int worker_done = 0;
static char body[BODY_MAX + 1];

static void
set_line(news_state_t state, const char* text) {
    mutex_lock(&news_mutex);
    status.state = state;
    snprintf(status.line, sizeof(status.line), "%s", text);
    mutex_unlock(&news_mutex);
}

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
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\nAccept: text/html\r\nConnection: close\r\n\r\n",
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

static void
run_fetch(void) {
    int count = 0, failed = 0;
    char reason[48] = "";

    for (int s = 0; s < SOURCES && count < NEWS_MAX && !abort_requested; s++) {
        struct sockaddr_in addr;
        size_t used;
        char line[48];

        snprintf(line, sizeof(line), "Reading %s...", sources[s].label);
        set_line(NEWS_RUNNING, line);
        if (!dcnow_resolve(sources[s].host, 80, &addr)) {
            snprintf(reason, sizeof(reason), "%s could not be found", sources[s].label);
            failed++;
            continue;
        }
        int code = http_get(&addr, sources[s].host, sources[s].path, WEB_TIMEOUT_MS, &used);
        if (code == -2) {
            set_line(NEWS_IDLE, "");
            return;
        }
        if (code != 200) {
            snprintf(reason, sizeof(reason), code >= 300 && code < 400 ? "%s wants https" : "%s did not answer", sources[s].label);
            failed++;
            continue;
        }
        const char* html = strstr(body, "\r\n\r\n");
        int room = NEWS_MAX - count < PER_SOURCE ? NEWS_MAX - count : PER_SOURCE;
        count += news_parse_html(html ? html + 4 : body, sources[s].label, parsed + count, room);
    }
    if (abort_requested) {
        set_line(NEWS_IDLE, "");
        return;
    }
    if (count == 0) {
        set_line(NEWS_FAILED, failed ? reason : "No news found on the pages");
        return;
    }
    mutex_lock(&news_mutex);
    memcpy(items, parsed, sizeof(parsed[0]) * (size_t)count);
    status.count = count;
    status.generation++;
    status.state = NEWS_DONE;
    snprintf(status.line, sizeof(status.line), "%s", failed ? reason : "");
    mutex_unlock(&news_mutex);
}

static void*
news_main(void* param) {
    (void)param;
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
news_fetch_start(void) {
    if (worker != NULL && !worker_done) {
        return;
    }
    join_worker();
    abort_requested = 0;
    worker_done = 0;
    set_line(NEWS_RUNNING, "Fetching news...");
    worker = thd_create(0, news_main, NULL);
    if (worker == NULL) {
        set_line(NEWS_FAILED, "Could not start the fetch");
    }
}

void
news_fetch_abort(void) {
    abort_requested = 1;
    join_worker();
    abort_requested = 0;
    mutex_lock(&news_mutex);
    if (status.state == NEWS_RUNNING) {
        status.state = NEWS_IDLE;
    }
    mutex_unlock(&news_mutex);
}

void
news_fetch_poll(news_status_t* out) {
    if (worker != NULL && worker_done) {
        join_worker();
    }
    mutex_lock(&news_mutex);
    *out = status;
    mutex_unlock(&news_mutex);
}

int
news_fetch_copy(news_item_t* out, int max) {
    mutex_lock(&news_mutex);
    int n = status.count < max ? status.count : max;
    memcpy(out, items, sizeof(items[0]) * (size_t)n);
    mutex_unlock(&news_mutex);
    return n;
}
