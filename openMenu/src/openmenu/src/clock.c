/* clock: see clock.h. */
#include <stdint.h>
#include <time.h>

#include <arch/rtc.h>

#include "clock.h"
#include "clock_math.h"

void
clock_get(int* year, int* month, int* day, int* hour, int* minute) {
    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    *year = t ? t->tm_year + 1900 : 2000;
    *month = t ? t->tm_mon + 1 : 1;
    *day = t ? t->tm_mday : 1;
    *hour = t ? t->tm_hour : 0;
    *minute = t ? t->tm_min : 0;
}

int
clock_set(int year, int month, int day, int hour, int minute) {
    return rtc_set_unix_secs((time_t)clock_unix_secs(year, month, day, hour, minute)) == 0 ? 0 : -1;
}

int
clock_date_order(void) {
    switch (*(volatile uint8_t*)0x8C000072 & 0xF) {
        case 0: return 0;
        case 2: return 2;
        default: return 1;
    }
}
