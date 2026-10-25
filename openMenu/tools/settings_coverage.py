#!/usr/bin/env python3
"""Does every save-file setting have a place in the Settings menu?

Reads the `extern uint8_t* sf_*` list of openmenu_settings.h (the file shared with Derek's openMenu: merge his new header, run this) and
checks each one is either a row's variable in ui_settings.c or listed in NOT_SHOWN below with the reason. Exit 1 on a new, unknown one.
Usage: python3 tools/settings_coverage.py   (from openMenu/)"""
import re, sys

HDR = "src/openmenu_settings/include/openmenu_settings.h"
ROWS = "src/openmenu/src/ui_settings.c"

# Derek's settings this menu does not offer on purpose (or not yet), each with why. Keep it honest: "not yet" ones are the to-do list.
NOT_SHOWN = {
    "sf_region": "not wanted: Derek's colour theme per region; the look is the BIOS's",
    "sf_ui": "not wanted: Derek's Style (LineDesc / Grid3 / Scroll / Folders); one BIOS look",
    "sf_custom_theme": "not wanted: Derek's themes",
    "sf_custom_theme_num": "not wanted: Derek's themes (20 slots in 1.7.1, kept so the choice survives a save)",
    "sf_scroll_art": "not wanted: Scroll style only",
    "sf_scroll_index": "not wanted: Scroll style only",
    "sf_disc_details": "not wanted: Scroll style only",
    "sf_folders_art": "not wanted: Folders style artwork (our list has its own art panel)",
    "sf_folder_art": "not wanted: Folders style artwork",
    "sf_folders_item_details": "not wanted: Folders style details",
    "sf_filter": "not yet: genre filter is the brief's Group control (needs genre data)",
    "sf_marquee_speed": "not yet: decide if the list scrolls long titles",
    "sf_mouse_cursor_speed": "not yet: with mouse support",
    "sf_mouse_scroll_speed": "not yet: with mouse support",
    "sf_serial_vmu_multislot": "not yet: Serial VMU Slots",
    "sf_music": "not yet: background music (BGM.ADP)",
    # data, not settings
    "sf_recent_games": "data: the recently played list",
    "sf_last_game": "data: remember last game",
    "sf_last_game_product": "data: remember last game",
    "sf_last_game_folder": "data: remember last game",
    "sf_last_game_filter": "data: remember last game",
}

hdr = open(HDR).read()
rows = open(ROWS).read()
bad = 0
for var in re.findall(r"extern uint8_t\*\s+(sf_\w+);", hdr):
    shown = re.search(r"&%s\b" % var, rows) is not None
    why = NOT_SHOWN.get(var)
    if shown and why:
        print(f"{var:26} row AND listed in NOT_SHOWN: remove it from the list"); bad += 1
    elif shown:
        print(f"{var:26} row")
    elif why:
        print(f"{var:26} no row: {why}")
    else:
        print(f"{var:26} NEW: no row and no reason. Add a row or a NOT_SHOWN entry"); bad += 1
sys.exit(1 if bad else 0)
