/*
 * sound: plays the BIOS menu sounds. The ARM7 sound driver and the sound effect
 * bank are read from the console's boot ROM and loaded into the sound chip at
 * start-up (see bios_audio.h). If anything is not as expected, sound stays off
 * and the menu works without it.
 */
#pragma once

#include <bios_rom.h>

/* Returns 0 when the driver and banks are loaded. */
int sound_init(const bios_rom* rom);

/* Play UI sound effect BAUDIO_SFX_* (ignored while sound is off or the driver is busy). */
void sound_sfx(int sfx);

void sound_stereo(int mono);

/* Short human readable state for the on-screen status line, e.g. "sound ok". */
const char* sound_status(void);

/* Stop the driver (ARM in reset). Call before handing over to a game. */
void sound_shutdown(void);
