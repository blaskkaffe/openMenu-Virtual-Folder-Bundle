/* clock_math: calendar arithmetic for the date and time editor. */
#pragma once

/* Seconds since 1970 for a calendar date (no leap seconds); negative before 1970. */
long long clock_unix_secs(int year, int month, int day, int hour, int minute);

/* Swatch Internet Time ("beats"): 1000 a day, from midnight in Biel (UTC+1). `utc_offset_min` is the console clock's offset from UTC
 * in minutes (east positive), the minute counts as its middle second. 0..999. */
int clock_beats(int hour, int minute, int utc_offset_min);

/* The text of the main menu's clock. mode: 0 12 hour, 1 24 hour, 2 date only, 3 12 hour + .beat, 4 24 hour + .beat (the setting
 * Clock: sf_clock). The date follows `date_order` (0 year first, 1 month first, 2 day first); with beats it has no year so the
 * line fits the header (at most 19 characters). Beats need to know the time zone: with offset_known 0 they are left out. */
void clock_header_text(char* out, unsigned size, int date_order, int mode, int year, int month, int day, int hour, int minute,
                       int utc_offset_min, int offset_known);
