/* clock_math: calendar arithmetic without any console dependency, see clock_math.h. */
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
