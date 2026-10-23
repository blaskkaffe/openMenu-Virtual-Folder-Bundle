/* online: see online.h. */
#include <stdbool.h>

#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include <arch/timer.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "backend/dcnow_fetch.h"
#include "backend/dcnow_net.h"
#include "backend/dcnow_vmu.h"
#include "backend/dreampi_link.h"
#include "backend/online_games.h"
#include "backend/online_time_sync.h"
#include "online.h"
#include "vmu_lcd_utils.h"

void
online_boot(void) {
    if (sf_dcnow[0] == DCNOW_AUTO_CONNECT) {
        dcnow_conn_autostart();
    }
}

void
online_set_hangup_hook(void (*hangup)(void)) {
    dcnow_net_set_hangup_hook(hangup);
}

/* A VMU plugged in while the menu runs gets the screen it should show (the main branch's vmu_lcd_check_insertions). */
#define VMU_LCD_CHECK_INTERVAL 120 /* about two seconds */

static void
vmu_lcd_check_insertions(void) {
    static bool prev_valid[8];
    static int counter;
    if (++counter < VMU_LCD_CHECK_INTERVAL) {
        return;
    }
    counter = 0;
    for (int port = 0; port < 4; port++) {
        for (int unit = 1; unit <= 2; unit++) {
            int idx = port * 2 + (unit - 1);
            maple_device_t* dev = maple_enum_dev(port, unit);
            bool now_valid = dev && dev->valid;
            if (now_valid && !prev_valid[idx] && (dev->info.functions & MAPLE_FUNC_LCD)) {
                if (savefile_lcd_owned()) {
                    dcnow_vmu_redraw();
                } else {
                    vmu_draw_lcd_auto(dev, (void*)savefile_lcd_logo());
                }
            }
            prev_valid[idx] = now_valid;
        }
    }
}

/* The top bar shows the player count, so the list is read once a link is up even when nobody has opened the Internet screen. The
 * online games code keeps it fresh when it can; with the DreamPi add-on it does not use the list, so this keeps it fresh then. */
static void
header_fetch_tick(void) {
    static uint64_t last_start;
    dcnow_status_t conn;
    dcnow_fetch_status_t fetch;

    if (sf_dcnow[0] == DCNOW_OFF) {
        return;
    }
    dcnow_conn_poll(&conn);
    if (conn.state != DCNOW_CONN_ONLINE) {
        return;
    }
    dcnow_fetch_poll(&fetch);
    if (fetch.state == DCNOW_FETCH_RUNNING) {
        last_start = timer_ms_gettime64();
        return;
    }
    if (!fetch.list_valid && fetch.state == DCNOW_FETCH_IDLE) {
        last_start = timer_ms_gettime64();
        dcnow_fetch_start(); /* once per link: a failure leaves the state FAILED, not IDLE, until the Internet screen retries */
    } else if (fetch.list_valid && dreampi_link_playing_known()) {
        int seconds = dcnow_refresh_seconds(sf_dcnow_refresh[0]);
        if (seconds > 0 && timer_ms_gettime64() - last_start >= (uint64_t)seconds * 1000u) {
            last_start = timer_ms_gettime64();
            dcnow_fetch_start();
        }
    }
}

void
online_header_get(online_header* out) {
    dcnow_status_t conn;
    dcnow_fetch_status_t fetch;

    out->shown = sf_dcnow[0] != DCNOW_OFF;
    out->state = 0;
    out->players = -1;
    if (!out->shown) {
        return;
    }
    dcnow_conn_poll(&conn);
    switch (conn.state) {
        case DCNOW_CONN_ONLINE: out->state = 2; break;
        case DCNOW_CONN_CONNECTING: out->state = conn.hanging_up ? 0 : 1; break;
        case DCNOW_CONN_FAILED: out->state = 3; break;
        default: break;
    }
    if (out->state == 2) {
        dcnow_fetch_poll(&fetch);
        if (fetch.list_valid) {
            out->players = fetch.online_total;
        }
    }
}

void
online_tick(int menu_idle) {
    vmu_lcd_check_insertions();
    header_fetch_tick();
    dcnow_conn_tick();
    dreampi_link_ui_idle(menu_idle);
    dreampi_link_tick();
    online_games_tick();
    dcnow_vmu_tick();
    online_time_sync_tick();
}

void
online_shutdown(void) {
    dcnow_net_shutdown();
}
