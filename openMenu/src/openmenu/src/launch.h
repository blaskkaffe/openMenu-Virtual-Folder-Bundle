/*
 * File: launch.h
 * Mount a GDEMU disc image and hand control to the game.
 */
#pragma once

#include <backend/gd_item.h>

/* Mount the image for `disc` and start it. Only returns if the launch failed. */
void launch_disc(const gd_item* disc);

/* PlayStation discs (type psx) start in Bleem! (BLEEM.BIN), or in Bloom (BLOOM.BIN) when it is on the menu disc and
 * chosen; the choice lasts until the menu is left. */
int launch_bloom_available(void);
void launch_set_psx_emulator(int bloom);
int launch_psx_emulator(void);
