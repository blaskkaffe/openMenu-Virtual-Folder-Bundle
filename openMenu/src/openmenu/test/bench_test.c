/* Host test of bench.c: key script parsing, a full simulated run, drop counting and the report lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"

static int fails;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static char log_text[200000];
static size_t log_len;

static void
out(const char* line) {
    size_t n = strlen(line);
    if (log_len + n + 2 < sizeof(log_text)) {
        memcpy(log_text + log_len, line, n);
        log_len += n;
        log_text[log_len++] = '\n';
        log_text[log_len] = '\0';
    }
}

static int
count(const char* needle) {
    int n = 0;
    for (const char* p = log_text; (p = strstr(p, needle)) != NULL; p++) n++;
    return n;
}

int
main(void) {
    /* keys */
    CHECK(bench_parse_keys("0:A", 0) == 'A');
    CHECK(bench_parse_keys("0:A", 1) == 0);
    CHECK(bench_parse_keys("0:Dx40/6", 0) == 'D');
    CHECK(bench_parse_keys("0:Dx40/6", 6) == 'D');
    CHECK(bench_parse_keys("0:Dx40/6", 7) == 0);
    CHECK(bench_parse_keys("0:Dx40/6", 234) == 'D');
    CHECK(bench_parse_keys("0:Dx40/6", 240) == 0);
    CHECK(bench_parse_keys("0:R 40:D 80:L", 80) == 'L');
    CHECK(bench_parse_keys("0:Y 30:Rx8/12 140:Y", 54) == 'R');
    CHECK(bench_parse_keys("bad", 0) == 0);
    CHECK(bench_parse_keys("", 0) == 0);

    /* a whole run: every frame 16.7 ms, except one slow frame per measured step */
    bench_start(2, 60, "test", out);
    CHECK(bench_active());
    int frames = 0, keys = 0, cmds = 0;
    bench_mem mem = {1000000, 500000, 1500000, 2000000};
    while (bench_active() && frames < 100000) {
        bench_cmd cmd;
        int k = bench_tick(&cmd);
        keys += k != 0;
        cmds += cmd.valid;
        bench_frame f;
        memset(&f, 0, sizeof(f));
        f.period_us = (frames % 97 == 50) ? 40000 : 16683;
        f.wait_us = 5000;
        f.build_us = 8000;
        f.logic_us = 1000;
        f.tris = 2000;
        mem.heap_used += 16; /* a slow leak */
        bench_frame_done(&f, "main", bench_wants_mem() ? &mem : NULL);
        frames++;
    }
    CHECK(!bench_active());
    CHECK(cmds == 2 * bench_step_count());
    CHECK(keys > 50);
    CHECK(count("OM bench begin") == 2 * bench_step_count());
    CHECK(count("OM bench step ") == 2 * bench_step_count());
    CHECK(count("OM bench cpu ") == 2 * bench_step_count());
    CHECK(count("OM bench mem ") == 2 * bench_step_count());
    CHECK(count("OM bench run ") == 2);
    CHECK(count("OM bench done complete") == 1);
    CHECK(count("drops=0 ") + count("drops=1 ") + count("drops=2 ") + count("drops=3 ") + count("drops=4 ") > 0);
    CHECK(strstr(log_text, "fps=59.") || strstr(log_text, "fps=60.") || strstr(log_text, "fps=58."));
    CHECK(strstr(log_text, "busy=") != NULL);

    /* stopping half way reports what there is */
    log_len = 0;
    log_text[0] = '\0';
    bench_start(1, 50, "", out);
    for (int i = 0; i < 300; i++) {
        bench_cmd cmd;
        bench_tick(&cmd);
        bench_frame f = {20000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        bench_frame_done(&f, "games", NULL);
    }
    bench_stop("stopped by START");
    CHECK(!bench_active());
    CHECK(count("OM bench done stopped by START") == 1);
    CHECK(strstr(log_text, "refresh=50Hz drop>30ms") != NULL);

    if (fails) {
        printf("%d failed\n%s", fails, log_text);
        return 1;
    }
    printf("bench_test: ok\n");
    return 0;
}
