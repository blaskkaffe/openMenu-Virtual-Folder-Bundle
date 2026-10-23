/* bench: the repeatable performance test, see bench.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"

#define SETTLE_FRAMES 45 /* before each step: the screen settles, the serial lines are written */
#define MAX_FRAMES 480   /* the longest step */
#define MAX_DROPS 8      /* dropped frames listed per step */

typedef struct bench_step {
    const char* name;
    int force_main;
    int icon; /* -1: stay where the last step left off */
    int reset_list;
    int frames;
    const char* keys; /* "<frame>:<key>" or "<frame>:<key>x<count>/<every>", space separated */
} bench_step;

/* The script. Never A on a game row (that would start it) and never X on the Internet screen (that would dial). */
static const bench_step steps[] = {
    {"main_idle", 1, 0, 0, 180, ""},
    {"main_move", 0, -1, 0, 240, "0:R 40:D 80:L 120:U 160:R 200:L"},
    {"games_open", 0, 0, 1, 120, "0:A"},
    {"games_idle", 0, -1, 0, 180, ""},
    {"games_scroll", 0, -1, 0, 300, "0:Dx40/6"},
    {"games_scroll_fast", 0, -1, 0, 240, "0:Ux100/2"},
    {"games_page", 0, -1, 0, 320, "0:Px6/25 160:Qx6/25"},
    {"viewer", 0, -1, 0, 320, "0:Y 30:Rx8/12 140:Y 190:Y 240:Lx4/10"},
    {"viewer_close", 0, -1, 0, 60, "0:B"},
    {"options_popup", 0, -1, 0, 120, "0:X 20:Dx3/15 90:B"},
    {"games_leave", 0, -1, 0, 120, "0:B 40:B"},
    {"settings", 1, 3, 0, 320, "0:A 40:Dx20/10 270:B"}, /* B saves the settings: the VMU write is part of it */
    {"files", 1, 1, 0, 400, "0:A 240:B 300:B"},
    {"internet", 1, 2, 0, 260, "0:A 200:B"}, /* Music and the CD player when the online features are off */
    {"main_end", 1, 0, 0, 180, ""},
};
#define NUM_STEPS ((int)(sizeof(steps) / sizeof(steps[0])))

typedef struct step_result {
    int frames;
    unsigned periods[MAX_FRAMES];
    unsigned long long sum_period, sum_logic, sum_build, sum_wait, sum_gpu, sum_ta, sum_busy;
    unsigned long long sum_vtx, sum_hdr, sum_tris;
    unsigned max_logic, max_build, art_loads, art_us, text_uploads;
    int drops;
    int ndrop;
    struct {
        int frame;
        bench_frame f;
    } drop[MAX_DROPS];
    char screen[24];
    bench_mem mem0, mem1;
    int have_mem0;
} step_result;

static bench_out_fn out_fn;
static int active, runs_total, run, step, phase_frame, measuring;
static unsigned drop_us;
static step_result cur;
static bench_mem run_mem0, first_mem;
static int have_first_mem;
static int run_drops, total_drops;
static unsigned run_worst, total_worst;
static const char* run_worst_step;
static char status[64];

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

static void
say(const char* fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (out_fn) {
        out_fn(line);
    }
}

int
bench_step_count(void) {
    return NUM_STEPS;
}

int
bench_parse_keys(const char* keys, int at_frame) {
    const char* p = keys;
    while (p && *p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char* end;
        long off = strtol(p, &end, 10);
        if (end == p || *end != ':' || !end[1]) {
            return 0; /* malformed: nothing */
        }
        int key = (unsigned char)end[1];
        p = end + 2;
        long count = 1, every = 1;
        if (*p == 'x') {
            count = strtol(p + 1, &end, 10);
            p = end;
            if (*p == '/') {
                every = strtol(p + 1, &end, 10);
                p = end;
            }
        }
        if (every < 1) every = 1;
        if (at_frame >= off && (at_frame - off) % every == 0 && (at_frame - off) / every < count) {
            return key;
        }
        while (*p && *p != ' ') p++;
    }
    return 0;
}

static int
cmp_unsigned(const void* a, const void* b) {
    unsigned x = *(const unsigned*)a, y = *(const unsigned*)b;
    return x < y ? -1 : x > y;
}

/* tenths of a millisecond from microseconds, as "12.3" */
static const char*
ms(unsigned long long us, char* buf) {
    unsigned t = (unsigned)((us + 50) / 100);
    sprintf(buf, "%u.%u", t / 10, t % 10);
    return buf;
}

static void
report_step(void) {
    const bench_step* s = &steps[step];
    step_result* r = &cur;
    int n = r->frames > 0 ? r->frames : 1;
    char a[16], b[16], c[16], d[16], e[16], f[16];
    unsigned sorted[MAX_FRAMES];
    memcpy(sorted, r->periods, (size_t)r->frames * sizeof(unsigned));
    qsort(sorted, (size_t)r->frames, sizeof(unsigned), cmp_unsigned);
    unsigned p50 = r->frames ? sorted[r->frames / 2] : 0;
    unsigned p95 = r->frames ? sorted[(r->frames * 95) / 100 < r->frames ? (r->frames * 95) / 100 : r->frames - 1] : 0;
    unsigned p99 = r->frames ? sorted[(r->frames * 99) / 100 < r->frames ? (r->frames * 99) / 100 : r->frames - 1] : 0;
    unsigned worst = r->frames ? sorted[r->frames - 1] : 0;
    unsigned fps10 = r->sum_period ? (unsigned)(10000000ULL * (unsigned long long)r->frames / r->sum_period) : 0;

    say("OM bench step %d.%d %s screen=%s frames=%d fps=%u.%u drops=%d worst=%s p50=%s p95=%s p99=%s ms", run + 1, step + 1,
        s->name, r->screen[0] ? r->screen : "?", r->frames, fps10 / 10, fps10 % 10, r->drops, ms(worst, a), ms(p50, b),
        ms(p95, c), ms(p99, d));
    unsigned busy = r->sum_period ? (unsigned)(100ULL * r->sum_busy / r->sum_period) : 0;
    say("OM bench cpu %d.%d busy=%u%% logic=%s/%s build=%s/%s wait=%s gpu=%s ms", run + 1, step + 1, busy,
        ms(r->sum_logic / (unsigned)n, a), ms(r->max_logic, b), ms(r->sum_build / (unsigned)n, c), ms(r->max_build, d),
        ms(r->sum_wait / (unsigned)n, e), ms(r->sum_gpu / (unsigned)n, f));
    say("OM bench load %d.%d ta=%s ms vtx=%llu hdr=%llu tris=%llu art=%u/%sms text=%u", run + 1, step + 1,
        ms(r->sum_ta / (unsigned)n, a), r->sum_vtx / (unsigned)n, r->sum_hdr / (unsigned)n, r->sum_tris / (unsigned)n,
        r->art_loads, ms(r->art_us, b), r->text_uploads);
    if (r->have_mem0) {
        say("OM bench mem %d.%d heap_used=%u (%+d) heap_free=%u arena=%u vram_free=%u (%+d)", run + 1, step + 1,
            r->mem1.heap_used, (int)(r->mem1.heap_used - r->mem0.heap_used), r->mem1.heap_free, r->mem1.arena,
            r->mem1.vram_free, (int)(r->mem1.vram_free - r->mem0.vram_free));
    }
    for (int i = 0; i < r->ndrop; i++) {
        const bench_frame* df = &r->drop[i].f;
        say("OM bench drop %d.%d f=%d %s ms logic=%s build=%s wait=%s art=%u text=%u", run + 1, step + 1, r->drop[i].frame,
            ms(df->period_us, a), ms(df->logic_us, b), ms(df->build_us, c), ms(df->wait_us, d), df->art_loads,
            df->text_uploads);
    }
    run_drops += r->drops;
    total_drops += r->drops;
    if (worst > run_worst) {
        run_worst = worst;
        run_worst_step = s->name;
    }
    if (worst > total_worst) {
        total_worst = worst;
    }
}

static void
report_run(void) {
    char a[16];
    say("OM bench run %d/%d drops=%d worst=%s ms in %s heap_used=%u (%+d since the run began, %+d since the test began)",
        run + 1, runs_total, run_drops, ms(run_worst, a), run_worst_step ? run_worst_step : "-", cur.mem1.heap_used,
        (int)(cur.mem1.heap_used - run_mem0.heap_used), (int)(cur.mem1.heap_used - first_mem.heap_used));
}

void
bench_start(int runs, unsigned refresh_hz, const char* info, bench_out_fn out) {
    out_fn = out;
    active = 1;
    runs_total = runs < 1 ? 1 : runs;
    run = 0;
    step = 0;
    phase_frame = 0;
    measuring = 0;
    drop_us = refresh_hz ? 1500000u / refresh_hz : 25000u;
    total_drops = 0;
    total_worst = 0;
    have_first_mem = 0;
    run_drops = 0;
    run_worst = 0;
    run_worst_step = NULL;
    memset(&cur, 0, sizeof(cur));
    say("OM bench start steps=%d runs=%d refresh=%uHz drop>%ums %s", NUM_STEPS, runs_total, refresh_hz, drop_us / 1000u,
        info ? info : "");
}

int
bench_active(void) {
    return active;
}

static void
finish(const char* why) {
    char a[16];
    say("OM bench done %s drops=%d worst=%s ms", why, total_drops, ms(total_worst, a));
    active = 0;
    status[0] = '\0';
}

void
bench_stop(const char* why) {
    if (!active) {
        return;
    }
    if (measuring && cur.frames > 0) {
        report_step();
    }
    finish(why && why[0] ? why : "stopped");
}

int
bench_wants_mem(void) {
    if (!active) {
        return 0;
    }
    /* at the start of settling (before), and on the last measured frame (after) */
    return (!measuring && phase_frame == 1) || (measuring && phase_frame == steps[step].frames - 1);
}

int
bench_tick(bench_cmd* cmd) {
    cmd->valid = 0;
    if (!active) {
        return 0;
    }
    const bench_step* s = &steps[step];
    if (!measuring) {
        if (phase_frame == 0) {
            say("OM bench begin %d.%d %s", run + 1, step + 1, s->name);
            cmd->valid = 1;
            cmd->force_main = s->force_main;
            cmd->icon = s->icon;
            cmd->reset_list = s->reset_list;
            snprintf(status, sizeof(status), "Performance test %d/%d: %s  (START stops)", step + 1, NUM_STEPS, s->name);
        }
        return 0;
    }
    return bench_parse_keys(s->keys, phase_frame);
}

void
bench_frame_done(const bench_frame* f, const char* screen, const bench_mem* mem) {
    if (!active) {
        return;
    }
    if (!measuring) {
        if (mem && phase_frame == 1) {
            cur.mem0 = *mem;
            cur.have_mem0 = 1;
            if (step == 0) {
                run_mem0 = *mem;
                if (!have_first_mem) {
                    first_mem = *mem;
                    have_first_mem = 1;
                }
            }
        }
        if (++phase_frame >= SETTLE_FRAMES) {
            measuring = 1;
            phase_frame = 0;
            status[0] = '\0';
        }
        return;
    }

    step_result* r = &cur;
    if (r->frames < MAX_FRAMES) {
        r->periods[r->frames] = f->period_us;
    }
    if (phase_frame > 0) { /* the first frame's period still holds the settling frame before it */
        r->frames++;
        r->sum_period += f->period_us;
        r->sum_busy += f->period_us > f->wait_us ? f->period_us - f->wait_us : 0;
        if (f->period_us > drop_us) {
            r->drops++;
            if (r->ndrop < MAX_DROPS) {
                r->drop[r->ndrop].frame = phase_frame;
                r->drop[r->ndrop].f = *f;
                r->ndrop++;
            }
        }
        r->sum_logic += f->logic_us;
        r->sum_build += f->build_us;
        r->sum_wait += f->wait_us;
        r->sum_gpu += f->gpu_us;
        r->sum_ta += f->ta_us;
        r->sum_vtx += f->vertices;
        r->sum_hdr += f->headers;
        r->sum_tris += f->tris;
        r->max_logic = f->logic_us > r->max_logic ? f->logic_us : r->max_logic;
        r->max_build = f->build_us > r->max_build ? f->build_us : r->max_build;
        r->art_loads += f->art_loads;
        r->art_us += f->art_us;
        r->text_uploads += f->text_uploads;
    }
    snprintf(r->screen, sizeof(r->screen), "%s", screen ? screen : "");
    if (mem) {
        r->mem1 = *mem;
    }

    if (++phase_frame < steps[step].frames) {
        return;
    }
    /* the step is over: report (the next frame is settling time, so the slow serial write costs nothing measured) */
    if (!r->have_mem0) {
        r->mem1 = r->mem0;
    }
    report_step();
    bench_mem last = r->mem1;
    memset(r, 0, sizeof(*r));
    r->mem1 = last;
    measuring = 0;
    phase_frame = 0;
    if (++step >= NUM_STEPS) {
        report_run();
        step = 0;
        run_drops = 0;
        run_worst = 0;
        run_worst_step = NULL;
        if (++run >= runs_total) {
            finish("complete");
        }
    }
}

const char*
bench_status(void) {
    return active ? status : "";
}
