/* Video mode of the launcher, see video.h. */
#include <dc/flashrom.h>
#include <dc/video.h>

#include "video.h"

static int refresh_hz = 60;

void
video_init(void) {
    if (vid_check_cable() == CT_VGA) {
        vid_set_mode(DM_640x480_VGA, PM_RGB565);
        refresh_hz = 60;
    } else if (flashrom_get_region() == FLASHROM_REGION_EUROPE) {
        vid_set_mode(DM_640x480_PAL_IL, PM_RGB565);
        refresh_hz = 50;
    } else {
        vid_set_mode(DM_640x480_NTSC_IL, PM_RGB565);
        refresh_hz = 60;
    }
}

int
video_refresh_hz(void) {
    return refresh_hz;
}
