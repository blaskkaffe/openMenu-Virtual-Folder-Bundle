/*
 * serial_region: is a game a PAL or an NTSC release, judged by its serial number (the product
 * code as listed, without dashes). Portable C.
 *
 * Sega's own games: HDR... is Japanese; MK + 5 digits is American (MK51035) and MK + 7 digits is
 * European (MK5103550, or a language variant such as MK5109505). Third party games end in a
 * letter: M Japan, N America, D Europe (T8102D, T8111D50, T45001D05); a European release of an
 * American serial carries two digits after the letter (T8103N50, language variant T8103N18).
 */
#pragma once

/* 1 for PAL, 0 for NTSC (also for serials it does not recognise). */
int serial_is_pal(const char* serial);
