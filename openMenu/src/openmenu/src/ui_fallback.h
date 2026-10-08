/* Plain framebuffer game list, used when the BIOS ROM cannot be used for the BIOS-style menu. */
#pragma once

#include <stdint.h>

/* `status` is shown on the bottom line. Never returns. */
void ui_fallback_run(const char* status);
