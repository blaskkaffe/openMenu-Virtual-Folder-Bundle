# Roadmap: the project brief against the code (dev, 65878e8; updated after the controls and 1.7.1 work)

Status of every part of the project brief (full text, ideas and roadmap: `PROJECT_BRIEF.md`), checked against the source on `dev`. Keys: **Done**, **Partial**,
**Missing**, **Blocked** (needs firmware, a DreamPi-side change or data we do not have yet), **Conflict** (the code does
something the brief asks to do differently). Hardware-test results are in `TEST_CHECKLIST.md`; this file only says what
exists in code.

Since this was written: the game list and case viewer controls follow the brief (decisions 1-3 as proposed), the
viewer shows genre, online, accessories and the META.DAT description, the save file matches Derek's 1.7.1, PSX discs and
VM2 game IDs work as in 1.7.1, settings can come from a stock Card Manager (`STOCK_CARD_MANAGER.md`), and there is a
repeatable performance test (NEWMENU.md "Performance test").

Storage rule from the 1.7.1 work: `OPENMENU.SYS` is shared with Derek's menu, so this branch must not add variables to
it (a version step there would make 1.7.1 treat it as unknown). Our own saved data (per-game network, favourites,
background colour, dark mode) needs a separate file on the VMU / SD, or keys in DEFAULTS.INI `[BIOSMENU]`.

Serial VMU (`serial_vmu.c`) restores a game's VMU image from the serial SD card before the game starts and backs it up at the next start, with 1.7.1's file layout (one image per game; not yet: Serial VMU Slots, the fallback VMU picker, exit to BIOS). Written without a console build: untested.
Not in this branch yet from 1.7.1: Serial VMU Slots, background music (BGM.ADP), the theme styles (not wanted: the look is the BIOS's).

## Decisions needed before coding

1. **X and Y in the game list (Done, as proposed).** Today X moves the cursor to the bottom buttons (Recent is the first) and Y
   opens the case viewer. The brief wants X = game options popup. Proposal: X opens the options popup; the bottom row is
   reached with Down from the last row / B (see 2), as the brief's B hierarchy already implies.
2. **B hierarchy (Done, as proposed).** Today B in the list leaves the folder, then goes straight to the main menu. The brief
   wants list -> bottom row -> main menu. Inside a folder: folder up first, or bottom row first? Proposal: folder up
   first (keeps "[..]" and B consistent), bottom row once at the root.
3. **Case viewer keys (Done: d-pad Left/Right still turn).** Today: A, B and Y close, X snaps to the front, triggers zoom, d-pad/stick turn.
   Brief: A launches, B closes, Y snaps to the front then flips to the back, Up/Down scroll the info text, L/R zoom, stick
   turns. That leaves the d-pad for text only. Agree before changing.
4. **Where the extra metadata comes from.** META.DAT has players, VMU blocks, accessories, network (modem/BBA), genre
   bits and a 376-byte description. It has **no developer, publisher or release year**; `gd_item.date` is the disc's
   build date from IP.BIN. Options: (a) extend META.DAT in the Card Manager (the struct has 2 spare bytes, not enough;
   a new META2.DAT keyed by serial), (b) sort "release year" by the IP.BIN date as an approximation. Grouping by
   developer/publisher and the Y screen's credits need (a).
5. **Versions vs. discs.** Discs are grouped already (by serial set). Versions (PAL/NTSC/translation/serial or WIZnet
   patch) need a label per image. The Card Manager is the only place that sees the image; proposal: it writes a
   version tag (region + patch kind) into the INI per slot, openMenu groups by a normalised title.
6. **Per-game network setting storage.** Needs a small table in the save file (serial -> DCNow!/DCNET). The switch
   itself exists (`dreampi_link_select_network`, POST /dcnow, /dcnet to the add-on) but is not called from any UI yet.
   Precedence proposal: per-game choice > global choice; if the Pi is not reachable, launch anyway and say so.

## 3. Main menu

| Item | Status | Notes |
|---|---|---|
| BIOS look, icons, animations, sound | Done | ROM-driven; colours/font weight still tuned on hardware (checklist) |
| Music -> Internet | Done | only while Online features is On; Off keeps Music + CD player |
| Logo follows the online mode | Partial | normal / "Now!" behind the logo on Dreamcast Now! / "DCNET" instead of the wordmark when the DreamPi has DCNET selected (`gfx_set_logo_mode`); a Pi without the add-on counts as Dreamcast Now!; untested on a console, letters drawn to match |
| Blue swirl logo for PAL, custom LOGO.PVR | Done | header logo; the checklist notes the logo setting only affects game launch |
| Online player count in the taskbar | Partial | drawn into the header's text surface in the row above the clock, in the clock's font: a connection lamp and "DCNow! n DCNet n" while online (`update_clock` in ui_bios.c; the letters were drawn to match the ROM digits: `tools/gen_clock_letters.py`). `online.c` reads the list once a link is up. Rendered with the real engine code on the preview, untested on a console |
| Notifications (starred player/game/event) | Missing | `dreampi_link_event_banner()` exists but nothing draws it |
| Connectivity status, keyboard/mouse indicator, extra clock settings in the header | Partial | connectivity: the lamp (grey off, yellow connecting, green online, red failed). Clock: Settings > Display > Clock format (12 hour with AM/PM, 24 hour, date only, each also with Swatch .beat; .beat needs Online time sync for the time zone and uses sf_clock values 3 and 4, which 1.7.1 resets to 12 hour). Keyboard/mouse indicator goes on the File screen's icons (next) |
| 60 fps | Partial | OPEN item 2: still below 60 on hardware; DMA vertex path is in |

## 4. Play menu

| Item | Status | Notes |
|---|---|---|
| 7 rows | Done | one box, `UIL_VISIBLE` 7 (ui_list.h), engine up to 13; text size at 9+ rows untested on hardware |
| Spinning GD-ROM disc with the game's label | Done | DISC.DAT from the Card Manager, fallbacks to ICON.DAT / region disc |
| 3D jewel case with front art | Done | back art slot exists (`BMODEL_TEX_BACK`) but nothing fills it |
| Info beside the art | Partial | product, region, players, VMU blocks, disc count on the bottom buttons; no genre, no description. The brief also wants the favourite star and genre on the list itself |
| Favourite / star | Partial | 1.7.1 has no favorites, so this branch has its own (`favorites.c`): X popup > Favorite toggles, a "* " starts the title in the list, the Favorites bottom button opens the list (Y removes). Stored in its own file (SD `/sd/OPENMENU/FAVORITES.DAT`, memory card `OPENMENU.FAV`), not in OPENMENU.SYS. Written without a console build: untested. Not yet: the star in the viewer, a real star glyph, Favorites as a Group |
| Disc number on multi-disc | Done | 3D digits at the bottom right of the spinning CD (selected row only, no total); Compact mode, Left/Right pick the disc |
| Sort A-Z / SD order | Done | Settings > Game order, and the Sort button on the bottom row |
| Sort by release year | Partial | by the disc's IP.BIN build date (decision 4, option b): oldest first, no date last. Real release years need META2.DAT |
| Group control (Folder, Favourites, Genre, ...) | Partial | only Folder and Recently played. Genre filter code exists in gd_list.c (`list_set_genre`) but is off: genre data is unreliable (NEWMENU todo). Modem/BBA from META.DAT `network`; Serial and W5500 need version tags (decision 5); Homebrew/Tools/Browsers need a tag too (browsers are found by name today) |
| Sort and group as separate controls on the bottom row | Partial | bottom row, left to right: Recent, Favorites (the starred games, popup), Folders (top of the folder tree), Sort (A cycles A-Z / Year / SD order, shows the active one, saved), Group (shows None; says no other groups yet). Group needs the other groups first |
| Disc organisation setting | Done | Settings > Multi-disc games, Disc sets from |
| Version organisation | Missing | decision 5 |
| X popup: cheats | Missing | no cheat code in this branch (removed in Phase 0) |
| X popup: disc / version picker | Missing | disc pick exists as Left/Right on the row |
| X popup: per-game network | Missing | decision 6 |
| Y screen: rotatable case | Done | eased rotate/zoom, mouse drag |
| Y screen: info text, scroll, A launches, Y snap/flip | Missing / Conflict | decision 3; description is in META.DAT already |
| B hierarchy | Conflict | decision 2 |
| OPEN: crash after closing the viewer | Open | needs the serial "Unhandled exception" lines |

## 5. File menu

| Item | Status | Notes |
|---|---|---|
| BIOS File screen (cards, copy, delete, memory reset with icon/colour) | Done | ported from the ROM; untested on real cards |
| VMU icon and colour | Partial | only through memory reset (BIOS behaviour); no change without wiping |
| Serial VMU submenu (enable, slot, backup/restore) | Partial | Settings > Game list > Serial VMU picks the VMU socket; restore before launch and backup at start work as 1.7.1 (`serial_vmu.c`, untested on hardware). No File-menu submenu yet, no manual backup/load, no Slots |
| Expand VMU storage | Blocked | depends on the device (VM2/VMU Pro/pico2maple) |
| VMU slot selection, GameID box | Blocked | no protocol yet for VM2, VMU Pro, pico2maple |
| Controller type label per port | Partial | types can be read from maple function codes today; selecting/emulating a type is Blocked (BlueRetro/pico2maple firmware) |
| Bluetooth pairing per port | Blocked | firmware |

## 6. Internet menu

| Item | Status | Notes |
|---|---|---|
| Sections | Done (layout) | buttons: Online (players and games side by side, Y switches column, A on a game starts it if it is on the card), Play (browsers; the online game catalogue row says coming soon), Events, News (placeholder: needs the DreamPi companion), Settings (connection, interface, ISP / preferred network / test as coming soon rows). Untested: written without a console build |
| Players online | Done | Pi `/players` first, fallbacks; DCNow!/DCNET counts |
| Games being played | Partial | listed; A refreshes instead of launching the game. Launch by Pi queue works (`dreampi_link.c`) |
| Star player/game/event + notifications | Missing | needs Pi-side storage or a save-file list, plus the banner (see main menu) |
| Online game catalogue (features, connection methods, network) | Missing | needs a data source (DreamcastLive / Flycast DCNet lists), best prepared on the Pi |
| Events (DC99) | Partial | upcoming only; no current/upcoming split, no detail view |
| News | Missing | Pi-side scraper + endpoint first |
| Companion interface | Partial | DreamPiAutoToggle module: game list upload, launch queue, `/players`, `/api/events/upcoming`, NET, PLAYING. No written spec of the protocol |
| ISP settings editor | Missing | OPEN item 3; "coming soon" row; flash format partly decoded |
| DreamPi Ethernet / Wi-Fi settings | Blocked | Pi-side endpoints first |
| Network selection global + per game | Partial | the Pi switch exists, no UI (decision 6) |
| Interface tests (modem, BBA, serial, W5500) | Missing | connection engine covers modem/BBA/LAN/serial (as the main branch: modem, BBA, LAN); W5500 is in no engine here or on main |
| Serial PPP to DreamPi | Done in code | hardware test open (OPEN item 6) |

## 7. Settings menu

| Item | Status | Notes |
|---|---|---|
| Scrollable list, 4 rows, 3D icon, help box | Done | rows beyond the BIOS use digit icons until real icons exist |
| BIOS settings (language, clock, sound, auto start) | Done | written to flash as the BIOS does |
| Background colour choice, dark mode | Missing | `dcbg` takes the gradient from the ROM; needs a small palette table |
| Online rows that belong in the Internet menu | To move | brief: keep Internet settings in the Internet menu |
| Flash partition 2 compaction | Missing | OPEN item 4; risk grows with every flash write (clock, ISP editor) |

## Added by the consolidated brief (PROJECT_BRIEF.md)

Checked against the earlier tables above; only what is new or sharper:

- **Group control list** is longer: Developer, Publisher, Local multiplayer, Online multiplayer, Online features, Modem, Serial, W5500, Broadband (besides Folder, Favorites, Genre, Homebrew, Tools, Browsers). Developer/Publisher need decision 4; Modem/BBA come from META.DAT `network`; Serial/W5500 need version tags (decision 5). Nothing new is built yet.
- **Sort and group are separate, independently configurable controls**, and the active sort shows in the bottom row (Missing, see the Play menu table).
- **Internet > Play** also reaches a web browser; **Internet > Online** A joins/launches a game (Partial: A refreshes today).
- **Version picker** in the X popup appears only when several variants are installed; labels must be region + patch type, not filenames.
- **Optional, later:** opening the case and showing the disc on the Y screen.
- **Hardware items stay conditional**: pico2maple, BlueRetro, iBlueControlMod, VM2, VMU Pro need firmware first (Blocked rows above).
- The brief's phase order (Play, File/Settings, Internet, hardware) differs from the order below, which puts data-ready and no-hardware work first; keep the order below unless you want the brief's.

## Suggested order

Ordered by what unblocks the most and what can be done without new hardware or data:

1. Settle decisions 1-3 (keys), then change the game list and case viewer to the brief's mapping. Small, code only.
2. Y screen info panel from META.DAT (description, genre, players, accessories, network). Data exists.
3. Flash compaction (OPEN 4), before the ISP editor adds more flash writes.
4. X options popup: disc picker, then per-game network (decision 6), with the save-file table.
5. Favourites (save-file list) + Favourites group; re-enable the genre group once the genre data is reviewed.
6. Main menu player count and the event/notification banner (APIs exist).
7. Internet menu restructure to Online / Play / Events / Settings; Games: A launches.
8. ISP editor (OPEN 3).
9. Card Manager: version tags and META2.DAT (developer, publisher, year), then version grouping and the remaining groups.
10. Pi side: protocol spec, news, catalogue, starring, Ethernet/Wi-Fi.
11. Device integrations (VMU slot/GameID, controller modes, pairing) once their firmware has a protocol.

Waiting on you (hardware): OPEN 1 (crash log), OPEN 2 (fps numbers), OPEN 6 (serial tests).
