#include <stdio.h>
#include <string.h>

#include "dbglog.h"

#define MAX_ENTRIES 28

typedef struct {
    char label[20];
    unsigned secs, fps_min;
    dbglog_sample s;
} entry;

static entry entries[MAX_ENTRIES];
static int n_entries;
static char last_label[20];
static int settled; /* seconds seen under last_label */

#define MAX_MARKS 14
static struct {
    char name[10];
    unsigned ms;
} marks[MAX_MARKS];
static int n_marks;

void
dbglog_boot(const char* name, unsigned ms) {
    if (n_marks < MAX_MARKS) {
        snprintf(marks[n_marks].name, sizeof(marks[n_marks].name), "%s", name);
        marks[n_marks++].ms = ms;
    }
}

void
dbglog_boot_lines(char* a, char* b, size_t cap) {
    char* out[2] = {a, b};
    a[0] = b[0] = 0;
    size_t used[2] = {0, 0};
    for (int i = 1; i < n_marks; i++) {
        int line = used[0] < 30 ? 0 : 1;
        int n = snprintf(out[line] + used[line], cap - used[line], "%s%s %u", used[line] ? "  " : "", marks[i].name,
                         marks[i].ms - marks[i - 1].ms);
        if (n > 0 && used[line] + (size_t)n < cap) used[line] += (size_t)n;
    }
}

void
dbglog_clear(void) {
    n_entries = 0;
    last_label[0] = 0;
    settled = 0;
}

int
dbglog_count(void) {
    return n_entries;
}

void
dbglog_add(const char* label, const dbglog_sample* s) {
    if (strncmp(label, last_label, sizeof(last_label) - 1) != 0) {
        snprintf(last_label, sizeof(last_label), "%s", label);
        settled = 0;
    }
    if (settled++ == 0) {
        return;
    }
    entry* e = NULL;
    for (int i = 0; i < n_entries; i++) {
        if (!strcmp(entries[i].label, last_label)) {
            e = &entries[i];
            break;
        }
    }
    if (!e) {
        if (n_entries == MAX_ENTRIES) {
            return;
        }
        e = &entries[n_entries++];
        snprintf(e->label, sizeof(e->label), "%s", last_label);
        e->secs = 0;
        e->fps_min = s->fps;
    }
    if (settled == 2) { /* a new visit: count again */
        e->secs = 0;
        e->fps_min = s->fps;
    }
    e->secs++;
    if (s->fps < e->fps_min) e->fps_min = s->fps;
    e->s = *s;
}

static void
tenths(char* out, size_t cap, unsigned v) {
    snprintf(out, cap, "%u.%u", v / 10, v % 10);
}

void
dbglog_row(int i, char* left, char* right, size_t cap) {
    left[0] = right[0] = 0;
    if (i < 0 || i >= n_entries) {
        return;
    }
    const entry* e = &entries[i];
    char sy[12], bu[12], wa[12], gp[12], ta[12];
    tenths(sy, sizeof(sy), e->s.sync);
    tenths(bu, sizeof(bu), e->s.build);
    tenths(wa, sizeof(wa), e->s.wait);
    tenths(gp, sizeof(gp), e->s.gpu);
    tenths(ta, sizeof(ta), e->s.ta);
    snprintf(left, cap, "%-13.13s %2u/%2u %4ut", e->label, e->s.fps, e->fps_min, e->s.tris);
    snprintf(right, cap, "s%s b%s w%s g%s t%s v%u h%u %c", sy, bu, wa, gp, ta, e->s.vtx, e->s.hdr, e->s.autosort ? 'a' : 'p');
}

int
dbglog_text(char* out, size_t cap, const char* info) {
    int n = snprintf(out, cap, "DBG1 %s\nlabel|fps|min|tris|sync|build|wait|gpu|ta|vtx|hdr|auto|begin|bg|drawn|secs (ms x10)\n", info);
    n += snprintf(out + n, cap - (size_t)n, "boot(ms, since console start):");
    for (int i = 0; i < n_marks && n > 0 && (size_t)n < cap; i++) {
        n += snprintf(out + n, cap - (size_t)n, " %s=%u", marks[i].name, marks[i].ms);
    }
    n += snprintf(out + n, cap - (size_t)n, "\n");
    for (int i = 0; i < n_entries && n > 0 && (size_t)n < cap; i++) {
        const entry* e = &entries[i];
        n += snprintf(out + n, cap - (size_t)n, "%s|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u\n", e->label, e->s.fps, e->fps_min,
                      e->s.tris, e->s.sync, e->s.build, e->s.wait, e->s.gpu, e->s.ta, e->s.vtx, e->s.hdr, e->s.autosort,
                      e->s.beg, e->s.bg, e->s.drawn, e->secs);
    }
    return n < 0 ? 0 : ((size_t)n >= cap ? (int)cap - 1 : n);
}
