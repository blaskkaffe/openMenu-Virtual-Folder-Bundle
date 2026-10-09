/* Host test for the date arithmetic: gcc -Isrc test/clock_test.c src/clock_math.c */
#include <stdio.h>

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
    if (failures) {
        return 1;
    }
    printf("clock: all checks passed\n");
    return 0;
}
