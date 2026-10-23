/* Host test for the date arithmetic: gcc -Isrc test/clock_test.c src/clock_math.c */
#include <stdio.h>
#include <string.h>

#include "clock_math.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int
main(void) {
    CHECK(clock_unix_secs(1970, 1, 1, 0, 0) == 0);
    CHECK(clock_unix_secs(2000, 1, 1, 0, 0) == 946684800LL);
    CHECK(clock_unix_secs(2024, 2, 29, 12, 30) == 1709209800LL);
    CHECK(clock_unix_secs(1969, 12, 31, 23, 59) == -60);
    CHECK(clock_unix_secs(1950, 1, 1, 0, 0) == -631152000LL);
    CHECK(clock_unix_secs(2085, 12, 31, 23, 59) == 3660681540LL);
    /* beats: 12:00 UTC is 13:00 in Biel = @541.7 (the minute counts as its middle second: 542); UTC-5 showing 07:00 is 12:00 UTC */
    CHECK(clock_beats(12, 0, 0) == 542);
    CHECK(clock_beats(7, 0, -300) == 542);
    CHECK(clock_beats(23, 0, 0) == 0 || clock_beats(23, 0, 0) == 1); /* midnight in Biel */
    CHECK(clock_beats(0, 0, 60) == 0 || clock_beats(0, 0, 60) == 1);
    CHECK(clock_beats(22, 59, 0) == 999);
    {
        char t[32];
        clock_header_text(t, sizeof(t), 1, 1, 2026, 10, 10, 22, 27, 0, 1);
        CHECK(!strcmp(t, "10/10/2026 22:27"));
        clock_header_text(t, sizeof(t), 1, 0, 2026, 10, 10, 22, 27, 0, 1);
        CHECK(!strcmp(t, "10/10/2026 10:27 PM"));
        clock_header_text(t, sizeof(t), 1, 0, 2026, 10, 10, 0, 5, 0, 1);
        CHECK(!strcmp(t, "10/10/2026 12:05 AM"));
        clock_header_text(t, sizeof(t), 2, 4, 2026, 10, 3, 22, 27, 0, 1);
        CHECK(!strcmp(t, "03/10 22:27 .977"));
        clock_header_text(t, sizeof(t), 0, 3, 2026, 10, 3, 12, 0, 0, 1);
        CHECK(!strcmp(t, "10/03 12:00 PM .542"));
        clock_header_text(t, sizeof(t), 2, 4, 2026, 10, 3, 22, 27, 0, 0); /* zone unknown: no beats */
        CHECK(!strcmp(t, "03/10/2026 22:27"));
        clock_header_text(t, sizeof(t), 0, 2, 2026, 10, 3, 22, 27, 0, 1);
        CHECK(!strcmp(t, "2026/10/03"));
        clock_header_text(t, sizeof(t), 1, 3, 2026, 12, 31, 12, 59, 330, 1);
        CHECK(strlen(t) <= 19);
    }
    if (failures) {
        return 1;
    }
    printf("clock: all checks passed\n");
    return 0;
}
