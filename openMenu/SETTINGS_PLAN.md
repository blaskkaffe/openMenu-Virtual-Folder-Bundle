# Settings menu: plan (layout, compatibility, grouping, maintenance)

Status: proposal, the new layout is not built yet. Derek's list is from his real 1.7.1 source (DerekPascarella/openMenu-Virtual-Folder-Bundle,
`ui_menu_credits.c`): 33 rows, only Clock extender is new against 1.7.0. His 1.7.1 renamed three rows (Online Time Sync -> Online Clock Sync,
VMU Time Sync -> VMU Clock Sync, Clock extender -> Clock Checkpoint Extender); the variables are the same, this menu keeps its own labels.
Our copy of `openmenu_settings.h` matches his in every variable (checked with `tools/settings_coverage.py`).

## 1. Compatibility rules (unchanged, they decide the design)
- `OPENMENU.SYS` is shared with Derek's menu. This menu never adds a variable to it and never changes one of his values' meaning.
  Settings he has and we do not show stay in the file untouched (we load and save every variable).
- Our own settings live in the `EXTENDED.SYS` flags (same file as the favorites; the name replaces FAVORITES.DAT / OPENMENU.FAV, which are still read when it is missing) (`favorites.h`: .beat, background, dark mode), so his menu never sees them.
- `[BIOSMENU]` keys of `DEFAULTS.INI` (stock Card Manager) are made from row labels and choice names. Shortening a label would break
  existing cards, so rows get an explicit key (his variable name) and the old long label is kept as an alias. See section 5.

## 2. Derek's settings against this menu

| Derek's row (1.7.0 / 1.7.1) | Variable | Here |
|---|---|---|
| Style | sf_ui | not wanted (one BIOS look) |
| Theme | sf_region | not wanted |
| (custom theme, 20 slots in 1.7.1) | sf_custom_theme(_num) | not wanted, preserved |
| Play BGM | sf_music | not yet (needs BGM.ADP support) |
| Honor Menu Defaults | sf_honor_defaults | **Card defaults** |
| Aspect | sf_aspect | **Aspect ratio** |
| Exit to BIOS | sf_bios_3d | **Exit to BIOS** |
| Sort | sf_sort | **Game order** (A-Z, SD order, release year) and the Sort button |
| Filter (genre) | sf_filter | not yet (the brief's Group control) |
| Multi-Disc | sf_multidisc | **Multi-disc games** |
| Multi-Disc Grouping | sf_multidisc_grouping | **Disc sets from** |
| Artwork, Display Index Numbers, Disc Details, Game Artwork, Folder Artwork, Item Details | sf_scroll_art, sf_scroll_index, sf_disc_details, sf_folders_art, sf_folder_art, sf_folders_item_details | not wanted (Scroll / Folders styles) |
| Remember Last Game | sf_remember_last_game | **Remember last game** |
| Recently Played | sf_recently_played | **Recently played** |
| Clock | sf_clock | **Clock format** (adds .beat modes) |
| Clock extender (1.7.1) | sf_clock_extender | **Clock extender** |
| Marquee Speed | sf_marquee_speed | not yet (only if long titles scroll) |
| Mouse Cursor / Scroll Speed | sf_mouse_*_speed | not yet (with mouse support) |
| Boot Mode | sf_boot_mode | **Boot animation** |
| DC Now! | sf_dcnow | **Online features** (stays in Settings: gates the Internet screen) |
| DC Now! Auto-Refresh | sf_dcnow_refresh | Internet screen > Settings |
| DC Now! VMU Updates | sf_dcnow_vmu | Internet screen > Settings |
| Online Time Sync | sf_online_time_sync | Internet screen > Settings |
| Serial VMU | sf_serial_vmu | **Serial VMU** |
| Serial VMU Multi-Slot | sf_serial_vmu_multislot | not yet (Slots) |
| VMU Game ID | sf_vm2_send_all | **VM2 game ID** |
| VMU Time Sync | sf_vmu_time_sync | **VMU time sync** |
| VMU Beep on Save | sf_beep | **VMU beep on save** (added) |
| Save/Load, Credits | (actions) | saving happens when leaving; About |

Only in this fork: Language, Date and time, Sound, Auto start, Memory card clock (the BIOS's own, written to flash), Clock format .beat modes,
Background, Dark mode, Performance test, About (version, BIOS revision). Fork data with no setting: favorites, per-game network.
`tools/settings_coverage.py` prints this mapping from the real header and fails on a variable nobody decided about.

## 3. Layout (two options from the owner, with a recommendation)
Every row's value must fit: the longest are "On (Manual connect)" (19 characters), "Same folder only", "Show all discs", "On (UTC+12:45)".
- **A. One big box** like the game picker (7 lines, `bios_list`), the width of the Settings rows, all ~27 rows scrolling, group
  name as a divider line. Simple, but 27 rows is a long scroll.
- **B. Two levels (owner's choice, some settings may stay in the main box).** Five broad rows in the Settings-row style; A opens that category as the big box. With Derek's
  own visibility rules (rows that do not apply are hidden) every category is at most 7 rows, i.e. one box without scrolling.
  B goes back one level, as the brief's B hierarchy says. The same big-box list serves A and B, so A stays possible as a fallback.

Common to both:
- The information box is a separate box under the list, not part of the scroll box, and loses the purple/orange pill (it is text only now).
  It shows the group, n/total and the help line.
- The value pill is the setting's value and must change: sized to the row's longest value (measured), right aligned, label limited to
  about 14 characters so the two never meet. Long value names get a short form shown in the pill and the full text in the help box.
- Engine work: a row bar list with a text/pill split exists in `bios_list` (game picker); the Settings page (`bios_page`, 4 fixed slots,
  object ids of the BIOS) is replaced by it for the lists; the 5-row top level needs `BPAGE_SLOTS` 5 (or the same list engine with 5 lines).

## 4. The settings to sort (every one that exists or is planned)
Not grouped yet: the columns say what each does so they can be sorted by content. "Where" = what the setting touches.
Short label (<= 14 characters) is a suggestion; the long name stays as the help line and as the `DEFAULTS.INI` alias.

| # | Setting (short label) | Values | Where it acts | Use |
|---|---|---|---|---|
| 1 | Language | 6 | BIOS flash | set once |
| 2 | Date and time | editor | BIOS flash | set once |
| 3 | Sound | Stereo / Mono | BIOS flash | set once |
| 4 | Auto start | On / Off | BIOS flash | set once |
| 5 | Memory card clock | action | the VMUs | rare |
| 6 | Clock extender | Off / On | BIOS flash at each start | set once |
| 7 | VMU time sync | Off / On | clock from a VMU at start | set once |
| 8 | Online time sync | Off, UTC zones (39) | clock from the internet | set once, Internet screen |
| 9 | Clock format | 12 h, 24 h, date only, + .beat | top bar | set once |
| 10 | Aspect ratio | 4:3 / 16:9 | whole menu | set once |
| 11 | Background | 6 colours | whole menu | set once |
| 12 | Dark mode | Off / On | whole menu | set once |
| 13 | Game order | A-Z, SD order, year | game list | often (also the Sort button) |
| 14 | Multi-disc | Show all / Compact | game list | set once |
| 15 | Disc sets from | Anywhere / Same folder | game list | set once |
| 16 | Recently played | Off, 10..50 | Recent button | set once |
| 17 | Remember last | Off / On | game list start | set once |
| 18 | (Filter, planned) | genres | game list | with the Group control |
| 19 | Boot animation | Full, license, animation, fast | when a game starts | set once |
| 20 | Exit to BIOS | Standard, Alternate, Alternate 3D | what discs do on exit | set once |
| 21 | Serial VMU | Off, A1..D2 | game start, SD card | set once |
| 22 | (Serial VMU slots, planned) | Off / On | game start, SD card | with Slots |
| 23 | VM2 game ID | All, first, off | VM2 / VMU Pro | set once |
| 24 | VMU beep on save | Off / On | VMU (added) | set once |
| 25 | Online features | Off, manual, auto-connect | Internet screen exists, startup | set once |
| 26 | Player refresh | Off, 10..60 s | Internet screen | set once, Internet screen |
| 27 | VMU online | Off / On | VMU screen | set once, Internet screen |
| 28 | Card defaults | Off / On | DEFAULTS.INI at start | set once |
| 29 | Performance test | action | developer | rare |
| 30 | About | box | information | rare |
| 31-34 | (planned) Play BGM, Marquee speed, Mouse cursor speed, Mouse scroll speed | | | later |

Derek's visibility rules to port (a row only shows when it applies): Card defaults only when the disc has boot defaults, Play BGM only with a
BGM track, Serial VMU needs an SD card and no VM2 game ID with a VM2 present, VM2 game ID needs a VM2 and Serial VMU off, Serial VMU slots
need Serial VMU on, the Online rows need Online features on, VMU online needs a refresh time, the mouse speeds need a mouse.
Proposal for sorting, to react to (not decided): a first sort by *when you touch it* gives "set once" (most), "often" (Game order) and
"rare" (actions). A sort by *what it changes* gives clock/time (6-9), whole-menu look (10-12), the list (13-17), starting a game
(19-24), BIOS (1-5), network (8, 25-27), tools (28-30).

## 5. Easy to maintain and to follow Derek's fork
- One table (`rows[]` in `ui_settings.c`) is the only place a setting is described. Add fields: `category`, `key` (his variable name,
  e.g. `serial_vmu`; used by `DEFAULTS.INI`, old label-based keys still accepted), `origin` (Derek or fork), `visible()` (his
  `settings_option_visible` rules, ported 1:1), short `label` (<= 14 characters), short value names.
- The menu draws from the table: categories, the box and the pill need no per-row code. A new Derek row = one table line.
- `tools/settings_coverage.py`: after merging his `openmenu_settings.h`, run it; it names every new variable that has no row and no
  recorded reason, so nothing of his is forgotten or silently dropped. Run it in the checklist before a release.
- Rules stay: never reorder or renumber his enums; new fork choices go in the favorites flags.

## 6. Decisions needed
1. Which rows stay in the main box and which get a second level (decide from the list in section 4)?
2. Should the time rows (6-9) be one group, or stay with their neighbours?

## 7. Decided so far
- Two levels, but some settings may stay in the main box (to be seen).
- **Clock and date** is one group: Date and time, Clock format, Memory card clock, Clock extender, VMU time sync, Online time sync (built in the
  current list; Online time sync is also on the Internet screen, the same setting shown in two places).

## 8. Icons
Models the BIOS has that fit (checked on the host with every ROM model 0-119): lips (43), clock (44), speaker (45), question mark (46), the
memory card / VMU (62-70), the controller (17), the GD-ROM disc (61), the BACK marker (0), and in bios_models.h the globe, the phone and the
jewel case. Rules set by the owner: time and clock = clock, sound = speaker, memory card / saving = VMU. Where a setting is two things
("Memory card clock", "VMU time sync": clock and VMU; "VMU beep on save": speaker and VMU) the clock / speaker wins, since that is what the
setting changes. Suggestions for the rest: Display (aspect ratio, background, dark mode): no screen model exists, build a small TV in
`bios_models.c` like the phone, or the question mark until then; Game list: the GD-ROM disc (as the list's own discs) or the jewel case;
Boot animation: the GD-ROM disc; Exit to BIOS: the BACK marker model; Online features and Internet screen rows: the globe (phone for the
connection / ISP rows later); Card defaults, Performance test, About, Auto start: the question mark (the BIOS's "Other"); mouse and
controller speeds, when they come: the controller.
