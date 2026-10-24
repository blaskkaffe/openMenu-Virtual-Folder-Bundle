/* clock_math: calendar arithmetic without any console dependency, see clock_math.h. */
#include <stdio.h>

#include "clock_math.h"

/* Days since 1970-01-01 of a calendar date (Howard Hinnant's days_from_civil). */
static long long
days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

long long
clock_unix_secs(int year, int month, int day, int hour, int minute) {
    return days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL;
}

int
clock_beats(int hour, int minute, int utc_offset_min) {
    int utc = hour * 60 + minute - utc_offset_min;
    int bmt = ((utc + 60) % 1440 + 1440) % 1440;       /* minutes since midnight in Biel */
    return (bmt * 600 + 300) / 864 % 1000;             /* (seconds * 10) / 864, the middle of the minute */
}

void
clock_header_text(char* out, unsigned size, int date_order, int mode, int year, int month, int day, int hour, int minute,
                  int utc_offset_min, int offset_known) {
    int beats = (mode == 3 || mode == 4) && offset_known;
    /* The header's text surface shows about 17 characters from x 40: a 12 hour clock takes a two digit year (or, with beats, one
     * letter A/P) so that the end is not cut off. */
    int twelve = mode == 0 || mode == 3;
    int yr = twelve ? year % 100 : year;
    const char* yfmt = twelve ? "%02d" : "%04d";
    char date[24], time[16], ys[8];
    snprintf(ys, sizeof(ys), yfmt, yr);
    if (beats) { /* no year: the line has to fit the header */
        snprintf(date, sizeof(date), "%02d/%02d", date_order == 2 ? day : month, date_order == 2 ? month : day);
    } else if (date_order == 0) {
        snprintf(date, sizeof(date), "%s/%02d/%02d", ys, month, day);
    } else if (date_order == 2) {
        snprintf(date, sizeof(date), "%02d/%02d/%s", day, month, ys);
    } else {
        snprintf(date, sizeof(date), "%02d/%02d/%s", month, day, ys);
    }
    if (mode == 2) {
        snprintf(out, size, "%s", date);
        return;
    }
    if (mode == 0 || mode == 3) {
        int h12 = hour % 12 == 0 ? 12 : hour % 12;
        if (beats) {
            snprintf(time, sizeof(time), "%d:%02d%c", h12, minute, hour < 12 ? 'A' : 'P');
        } else {
            snprintf(time, sizeof(time), "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
        }
    } else {
        snprintf(time, sizeof(time), "%02d:%02d", hour, minute);
    }
    if (beats) {
        snprintf(out, size, "%s %s .%03d", date, time, clock_beats(hour, minute, utc_offset_min));
    } else {
        snprintf(out, size, "%s %s", date, time);
    }
}
