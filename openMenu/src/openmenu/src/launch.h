/*
 * File: launch.h
 * Mount a GDEMU disc image and hand control to the game.
 */
#pragma once

#include <backend/gd_item.h>

/* Mount the image for `disc` and start it. Only returns if the launch failed. */
void launch_disc(const gd_item* disc);
