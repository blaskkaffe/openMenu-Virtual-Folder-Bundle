/*
 * iblue_vmu: block access to the iBlueControlMod's settings channel through KallistiOS'
 * VMU calls. Looks at every memory card unit for the board's marker and uses the first one.
 */
#pragma once

#include "iblue_link.h"

/* Fill `io`. If no unit carries the marker the io still works and reads zeros, so the link
 * reports "no board". */
void iblue_vmu_io(iblue_io* io);
