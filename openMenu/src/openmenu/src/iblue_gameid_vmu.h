/* iblue_gameid_vmu: the block calls of iblue_gameid on the console (KallistiOS VMU functions). */
#pragma once

/* Tell the adapter which game starts ("" = none). Looks for the adapter once and remembers if there is
 * none, so a console without an iBlue pays for it only the first time. Returns 0 if it was applied. */
int iblue_gameid_set(const char* product);
