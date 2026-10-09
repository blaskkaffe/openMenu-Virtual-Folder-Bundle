/*
 * Video mode of the launcher. Like the BIOS, a PAL console gets a PAL 50 Hz mode (the 480
 * lines sit inside the 576 visible ones, so nothing is cropped by overscan), NTSC consoles
 * 480i at 60 Hz, and a VGA cable 640x480 progressive. The menu logic runs once per
 * display refresh in the BIOS, so the rate matters for animation speed.
 */
#pragma once

/* Select and set the video mode. Call once, before the PVR is initialised. */
void video_init(void);

/* Refresh rate of the mode chosen by video_init(): 50 or 60. */
int video_refresh_hz(void);
