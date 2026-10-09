/* clock_math: calendar arithmetic for the date and time editor. */
#pragma once

/* Seconds since 1970 for a calendar date (no leap seconds); negative before 1970. */
long long clock_unix_secs(int year, int month, int day, int hour, int minute);
