# Settings menu: plan (layout, compatibility, grouping, maintenance)

Status: proposal, nothing of the new layout is built yet. Source of Derek's list: his 1.7.0 menu (`origin/main`,
`ui_menu_credits.c`, 32 rows) plus what this repo's shared save header says 1.7.1 added (Clock extender, 20 theme slots).
His 1.7.1 menu source is not in this repo: if he added rows beyond those, diff against his tree and add them to the table.

## 1. Compatibility rules (unchanged, they decide the design)
- `OPENMENU.SYS` is shared with Derek's menu. This menu never adds a variable to it and never changes one of his values' meaning.
  Settings he has and we do not show stay in the file untouched (we load and save every variable).
- Our own settings live in the favorites file flags (`favorites.h`: .beat, background, dark mode), so his menu never sees them.
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
| VMU Beep on Save | sf_beep | **missing, easy**: add the row |
| Save/Load, Credits | (actions) | saving happens when leaving; About |

Only in this fork: Language, Date and time, Sound, Auto start, Memory card clock (the BIOS's own, written to flash), Clock format .beat modes,
Background, Dark mode, Performance test, About (version, BIOS revision). Fork data with no setting: favorites, per-game network.
`tools/settings_coverage.py` prints this mapping from the real header and fails on a variable nobody decided about.

## 3. Layout (two options from the owner, with a recommendation)
Every row's value must fit: the longest are "On (Manual connect)" (19 characters), "Same folder only", "Show all discs", "On (UTC+12:45)".
- **A. One big box** like the game picker (7 lines, `bios_list`), the width of the Settings rows, all ~27 rows scrolling, group
  name as a divider line. Simple, but 27 rows is a long scroll.
- **B. Two levels (recommended).** Five broad rows in the Settings-row style; A opens that category as the big box. With Derek's
  own visibility rules (rows that do not apply are hidden) every category is at most 7 rows, i.e. one box without scrolling.
  B goes back one level, as the brief's B hierarchy says. The same big-box list serves A and B, so A stays possible as a fallback.

Common to both:
- The information box is a separate box under the list, not part of the scroll box, and loses the purple/orange pill (it is text only now).
  It shows the group, n/total and the help line.
- The value pill is the setting's value and must change: sized to the row's longest value (measured), right aligned, label limited to
  about 14 characters so the two never meet. Long value names get a short form shown in the pill and the full text in the help box.
- Engine work: a row bar list with a text/pill split exists in `bios_list` (game picker); the Settings page (`bios_page`, 4 fixed slots,
  object ids of the BIOS) is replaced by it for the lists; the 5-row top level needs `BPAGE_SLOTS` 5 (or the same list engine with 5 lines).

## 4. Five categories
1. **Console** (BIOS, in flash): Language, Date and time, Sound, Auto start, Memory card clock, Clock extender (6).
2. **Display**: Clock format, Aspect ratio, Background, Dark mode; later Marquee speed, Mouse speeds (4-7).
3. **Game list**: Game order, Multi-disc games, Disc sets from, Recently played, Remember last game; later Filter (5-6).
4. **Starting games**: Boot animation, Exit to BIOS, Serial VMU, VM2 game ID, VMU time sync, VMU beep on save; later Serial VMU Slots (5-7).
5. **More**: Online features, Card defaults, Performance test, About; later Play BGM (4-5).

## 5. Easy to maintain and to follow Derek's fork
- One table (`rows[]` in `ui_settings.c`) is the only place a setting is described. Add fields: `category`, `key` (his variable name,
  e.g. `serial_vmu`; used by `DEFAULTS.INI`, old label-based keys still accepted), `origin` (Derek or fork), `visible()` (his
  `settings_option_visible` rules, ported 1:1), short `label` (<= 14 characters), short value names.
- The menu draws from the table: categories, the box and the pill need no per-row code. A new Derek row = one table line.
- `tools/settings_coverage.py`: after merging his `openmenu_settings.h`, run it; it names every new variable that has no row and no
  recorded reason, so nothing of his is forgotten or silently dropped. Run it in the checklist before a release.
- Rules stay: never reorder or renumber his enums; new fork choices go in the favorites flags.

## 6. Decisions needed
1. Option A or B (recommended: B)? Category names and rows as in section 4?
2. Add VMU Beep on Save now (needs checking what main does with `sf_beep`)?
3. Do you have Derek's 1.7.1 source or a list of its settings rows, to complete section 2?
