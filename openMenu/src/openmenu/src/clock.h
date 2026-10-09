/* clock: the console's real-time clock as plain date fields, for the date and time editor. */
#pragma once

/* Current date and time (the clock counts in UTC as far as the menu is concerned). */
void clock_get(int* year, int* month, int* day, int* hour, int* minute);

/* Set the clock. Returns 0 on success. */
int clock_set(int year, int month, int day, int hour, int minute);

/* Order the date is written in: 0 year first (Japan), 1 month first (America), 2 day first (Europe),
 * chosen by the console's region. */
int clock_date_order(void);
