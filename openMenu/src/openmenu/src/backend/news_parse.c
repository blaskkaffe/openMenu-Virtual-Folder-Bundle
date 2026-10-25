/*
 * File: news_parse.c
 * Project: openmenu
 * See news_parse.h.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/news_parse.h"

typedef struct {
    news_item_t* out;
    int max, count;
    const char* source;
    int active; /* a heading was seen: text belongs to the item being built */
    int in_head;
    char title[NEWS_TITLE_LEN];
    char body[NEWS_BODY_LEN];
    size_t tn, bn;
} parser;

static void
trim_end(char* s, size_t* n) {
    while (*n > 0 && s[*n - 1] == ' ') {
        s[--*n] = '\0';
    }
}

static void
commit(parser* ps) {
    trim_end(ps->title, &ps->tn);
    trim_end(ps->body, &ps->bn);
    if (ps->active && ps->tn > 0 && ps->count < ps->max) {
        news_item_t* it = &ps->out[ps->count++];
        snprintf(it->title, sizeof(it->title), "%s", ps->title);
        snprintf(it->body, sizeof(it->body), "%s", ps->body);
        snprintf(it->source, sizeof(it->source), "%s", ps->source);
    }
    ps->title[0] = ps->body[0] = '\0';
    ps->tn = ps->bn = 0;
}

/* One character of text into the heading or the body, white space collapsed. */
static void
put(parser* ps, char c) {
    char* buf = ps->in_head ? ps->title : ps->body;
    size_t* n = ps->in_head ? &ps->tn : &ps->bn;
    size_t cap = ps->in_head ? sizeof(ps->title) : sizeof(ps->body);
    if (!ps->active) {
        return;
    }
    if (c == '\t' || c == '\r' || c == '\n') {
        c = ' ';
    }
    if (c == ' ' && (*n == 0 || buf[*n - 1] == ' ')) {
        return;
    }
    if (*n + 1 < cap) {
        buf[(*n)++] = c;
        buf[*n] = '\0';
    }
}

static int
ci_prefix(const char* p, const char* word) {
    for (; *word; p++, word++) {
        if (tolower((unsigned char)*p) != *word) {
            return 0;
        }
    }
    return 1;
}

static const char*
ci_find(const char* p, const char* word) {
    for (; *p; p++) {
        if (ci_prefix(p, word)) {
            return p;
        }
    }
    return NULL;
}

/* Handles "&...;" at p. Returns how many input characters it used (0: not an entity). */
static int
entity(parser* ps, const char* p) {
    static const struct {
        const char* name;
        char c;
    } names[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&nbsp;", ' '}, {"&#39;", '\''},
                 {"&rsquo;", '\''}, {"&lsquo;", '\''}, {"&ldquo;", '"'}, {"&rdquo;", '"'}, {"&ndash;", '-'}, {"&mdash;", '-'}};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i].name);
        if (!strncmp(p, names[i].name, n)) {
            put(ps, names[i].c);
            return (int)n;
        }
    }
    if (p[1] == '#') {
        char* end;
        long v = p[2] == 'x' || p[2] == 'X' ? strtol(p + 3, &end, 16) : strtol(p + 2, &end, 10);
        if (*end == ';' && end - p < 10) {
            put(ps, v >= 32 && v < 127 ? (char)v : (v == 8217 || v == 8216 ? '\'' : v == 8220 || v == 8221 ? '"' : v == 8211 || v == 8212 ? '-' : ' '));
            return (int)(end - p) + 1;
        }
    }
    return 0;
}

/* The ASCII look-alike of a UTF-8 sequence at p (the font has ASCII only), and its length. */
static int
utf8(parser* ps, const unsigned char* p) {
    int n = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;
    char c = 0;
    if (p[0] == 0xE2 && p[1] == 0x80) {
        c = p[2] == 0x98 || p[2] == 0x99 ? '\'' : p[2] == 0x9C || p[2] == 0x9D ? '"' : p[2] == 0x93 || p[2] == 0x94 ? '-' : p[2] == 0xA6 ? '.' : 0;
    } else if (p[0] == 0xC2 && p[1] == 0xA0) {
        c = ' ';
    }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            n = i; /* cut sequence */
            break;
        }
    }
    if (c) {
        put(ps, c);
    }
    return n;
}

int
news_parse_html(const char* html, const char* source, news_item_t* out, int max) {
    parser* ps = calloc(1, sizeof(*ps));
    int count;
    if (!ps) {
        return 0;
    }
    ps->out = out;
    ps->max = max;
    ps->source = source;
    for (const char* p = html; *p;) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) {
                const char* e = strstr(p + 4, "-->");
                p = e ? e + 3 : p + strlen(p);
                continue;
            }
            int closing = p[1] == '/';
            const char* name = p + 1 + closing;
            const char* e = strchr(p, '>');
            if (!isalpha((unsigned char)*name)) { /* a lone '<' */
                put(ps, '<');
                p++;
                continue;
            }
            if (!e) {
                break;
            }
            if (!closing && (ci_prefix(name, "script") || ci_prefix(name, "style"))) {
                const char* end = ci_find(e, ci_prefix(name, "script") ? "</script" : "</style");
                p = end ? end : e + 1;
                continue;
            }
            if (name[0] != '\0' && (name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '4' && !isalnum((unsigned char)name[2])) {
                if (!closing) {
                    commit(ps);
                    ps->active = 1;
                    ps->in_head = 1;
                } else {
                    ps->in_head = 0;
                }
            } else if (ci_prefix(name, "p") || ci_prefix(name, "br") || ci_prefix(name, "div") || ci_prefix(name, "li") || ci_prefix(name, "tr") ||
                       ci_prefix(name, "td") || ci_prefix(name, "hr")) {
                put(ps, ' ');
            }
            p = e + 1;
        } else if (*p == '&') {
            int n = entity(ps, p);
            if (n) {
                p += n;
            } else {
                put(ps, '&');
                p++;
            }
        } else if ((unsigned char)*p >= 0x80) {
            p += utf8(ps, (const unsigned char*)p);
        } else {
            put(ps, *p++);
        }
    }
    commit(ps);
    count = ps->count;
    free(ps);
    return count;
}
