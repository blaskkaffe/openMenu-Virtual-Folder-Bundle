# Roadmap: the project brief against the code (dev, 65878e8; updated after the controls and 1.7.1 work)

Status of every part of the project brief, checked against the source on `dev`. Keys: **Done**, **Partial**,
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

Not in this branch yet from 1.7.1: the Serial VMU restore before a game (the setting is kept but not acted on),
background music (BGM.ADP), the theme styles (not wanted: the look is the BIOS's).

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
| Blue swirl logo for PAL, custom LOGO.PVR | Done | header logo; the checklist notes the logo setting only affects game launch |
| Online player count in the taskbar | Missing | count exists (`pstat.online_total` in ui_internet.c); needs drawing in the header row and a background fetch |
| Notifications (starred player/game/event) | Missing | `dreampi_link_event_banner()` exists but nothing draws it |
| Connectivity status, keyboard/mouse indicator, extra clock settings in the header | Missing | |
| 60 fps | Partial | OPEN item 2: still below 60 on hardware; DMA vertex path is in |

## 4. Play menu

| Item | Status | Notes |
|---|---|---|
| 7 rows | Conflict | `UIL_VISIBLE` is 5 (ui_list.h); brief says 7 |
| Spinning GD-ROM disc with the game's label | Done | DISC.DAT from the Card Manager, fallbacks to ICON.DAT / region disc |
| 3D jewel case with front art | Done | back art slot exists (`BMODEL_TEX_BACK`) but nothing fills it |
| Info beside the art | Partial | product, region, players, VMU blocks, disc count on the bottom buttons; no genre, no description |
| Favourite / star | Missing | needs a save-file list and a key |
| Disc number pill on multi-disc | Done | Compact mode, Left/Right pick the disc |
| Sort A-Z / SD order | Done | Settings > Game order |
| Sort by release year | Missing | see decision 4 |
| Group control (Folder, Favourites, Genre, ...) | Partial | only Folder and Recently played. Genre filter code exists in gd_list.c (`list_set_genre`) but is off: genre data is unreliable (NEWMENU todo). Modem/BBA from META.DAT `network`; Serial and W5500 need version tags (decision 5); Homebrew/Tools/Browsers need a tag too (browsers are found by name today) |
| Sort and group as separate controls on the bottom row | Missing | bottom buttons currently show info, button 1 = Recent |
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
| Serial VMU submenu (enable, slot, backup/restore) | Missing | needs the serial VMU emulation's interface on the SD side |
| Expand VMU storage | Blocked | depends on the device (VM2/VMU Pro/pico2maple) |
| VMU slot selection, GameID box | Blocked | no protocol yet for VM2, VMU Pro, pico2maple |
| Controller type label per port | Partial | types can be read from maple function codes today; selecting/emulating a type is Blocked (BlueRetro/pico2maple firmware) |
| Bluetooth pairing per port | Blocked | firmware |

## 6. Internet menu

| Item | Status | Notes |
|---|---|---|
| Sections | Conflict | code: Events, Players, Games, Browsers. Brief: Online (players + games), Play (online catalogue + browser), Events, Settings, plus News |
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
| Interface tests (modem, BBA, serial, W5500) | Missing | connection engine covers modem/BBA/LAN/serial; W5500 not in `dcnow_net.c` |
| Serial PPP to DreamPi | Done in code | hardware test open (OPEN item 6) |

## 7. Settings menu

| Item | Status | Notes |
|---|---|---|
| Scrollable list, 4 rows, 3D icon, help box | Done | rows beyond the BIOS use digit icons until real icons exist |
| BIOS settings (language, clock, sound, auto start) | Done | written to flash as the BIOS does |
| Background colour choice, dark mode | Missing | `dcbg` takes the gradient from the ROM; needs a small palette table |
| Online rows that belong in the Internet menu | To move | brief: keep Internet settings in the Internet menu |
| Flash partition 2 compaction | Missing | OPEN item 4; risk grows with every flash write (clock, ISP editor) |

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
