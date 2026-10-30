/*
 * iblue_brctl: the iblue_io on the console, over the brctl library (brctl/). Finds the adapter
 * once (a port where a Bluetooth controller is connected, if the adapter hides empty ports) and
 * keeps the answer until the next probe.
 */
#pragma once

#include "iblue_link.h"

/* Fill `io`. With no adapter its calls return IBLUE_ERR_NO_BOARD. probe!=0 looks again. */
void iblue_brctl_io(iblue_io* io, int probe);
