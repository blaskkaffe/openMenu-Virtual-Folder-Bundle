/* bench: the repeatable performance test (Settings > Other > Performance test).
 *
 * A fixed script goes through every screen with the same key presses at the same frames: main menu idle and moving,
 * game list open / idle / scrolling slow and fast / paging (cases flying), the case viewer turning and flipping, the
 * options popup, Settings, File, Internet (or Music), and the main menu again. Each step first settles for a moment
 * (not measured, the serial lines are written then), then measures every frame: frame time (fps, dropped frames, worst,
 * 50/95/99th percentile), CPU busy share, logic / build / PVR wait / GPU / TA times, vertices, triangles, texture
 * uploads and picture loads, heap and video RAM before and after. Results go to the serial port as "OM bench ..."
 * lines; "OM bench begin <step>" is written before a step starts, so a crash dump or a hang shows where it happened.
 * The script runs twice so a leak shows as heap that does not come back. START on the controller stops it.
 * tools/serial_capture.py --bench turns the lines into a report.
 *
 * Portable C (no KOS): the caller feeds it the numbers of each frame and turns its key letters into buttons. */
#pragma once

#include <stddef.h>

typedef struct bench_frame {
    unsigned period_us; /* from the start of the previous frame to the start of this one */
    unsigned logic_us;  /* input, logic steps and syncing of this frame */
    unsigned build_us;  /* drawing, without the wait for the PVR */
    unsigned wait_us;   /* waiting for the PVR (previous frame) and the display */
    unsigned gpu_us, ta_us;
    unsigned vertices, headers, tris;
    unsigned art_loads, art_us, text_uploads;
} bench_frame;

typedef struct bench_mem {
    unsigned heap_used, heap_free, arena, vram_free;
} bench_mem;

/* What the caller does when a step begins (before its first measured frame). */
typedef struct bench_cmd {
    int valid;
    int force_main; /* back to the main menu if not there (reported) */
    int icon;       /* -1, or the main menu icon to select */
    int reset_list; /* game list to its top folder, cursor on the first row */
} bench_cmd;

typedef void (*bench_out_fn)(const char* line);

/* Starts the script `runs` times. `refresh_hz` (50 or 60) sets what a dropped frame is (longer than 1.5 refreshes). */
void bench_start(int runs, unsigned refresh_hz, const char* info, bench_out_fn out);
int bench_active(void);
/* Stops it now (START pressed): what was measured so far is reported. */
void bench_stop(const char* why);

/* Once per frame, before input handling: the key letter to press this frame (0 none; U D L R A B X Y S, P page down,
 * Q page up). *cmd is valid on the first frame of a step's settling time. */
int bench_tick(bench_cmd* cmd);

/* The caller wants the memory numbers this frame (pass them to bench_frame). */
int bench_wants_mem(void);

/* Once per frame, after drawing: this frame's numbers, the screen it is on, the memory if bench_wants_mem() said so
 * (else NULL). */
void bench_frame_done(const bench_frame* f, const char* screen, const bench_mem* mem);

/* A short line for the screen while the test settles between steps ("" while measuring or idle). */
const char* bench_status(void);

/* For the host test: the number of steps, and the parsed key events of a script string. */
int bench_step_count(void);
int bench_parse_keys(const char* keys, int at_frame);
