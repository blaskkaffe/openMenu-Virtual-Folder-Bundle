# Using this openMenu with Derek's stock Card Manager (1.7.1)

This branch runs from a card built by the unmodified GD MENU Card Manager 1.7.1. The Card Manager copies everything in
its `tools/openMenu/menu_data` folder into the menu image, and it keeps the `DEFAULTS.INI` sections it does not know when
it rewrites that file. Both are what this branch uses.

## Setup

1. In the Card Manager's folder, open `tools/openMenu/menu_data` (on macOS, `DEFAULTS.INI` lives in the Card Manager's
   Application Support folder instead).
2. Replace `1ST_READ.BIN` with this branch's build (the CI artifact `1ST_READ.BIN`). Keep a copy of Derek's to go back.
3. Optional: put a `LOGO.PVR` there for your own header logo (`tools/bios-logo/biologo.py convert logo.png LOGO.PVR`).
4. Optional: add a `[BIOSMENU]` section to `DEFAULTS.INI` (below).
5. Save the card as usual. Updating openMenu from inside the Card Manager puts Derek's `1ST_READ.BIN` back: repeat step 2.

The save file (`OPENMENU.SYS` on the VMU, and its SD copy) is the same as 1.7.1's, so both menus can be swapped on one
card without losing settings.

## What works, what is ignored

| From the stock Card Manager | In this branch |
|---|---|
| `OPENMENU.INI` (games, folders, discs, regions, types) | used; PSX discs start in Bleem! / Bloom, `other` discs through the BIOS |
| `ICON.DAT`, `BOX.DAT`, `META.DAT` | used (row discs, CD case front, info screen) |
| `DISC.DAT` (each game's own disc label) | not made by the stock Card Manager: the row discs show the ICON.DAT art, else the region's BIOS disc |
| `BLEEM.BIN`, `BLOOM.BIN` in menu_data | used for PSX discs |
| Menu Options: style and theme (`[DEFAULTS]`) | ignored: the look is the console's BIOS |
| Menu Options: background music (`[BGM]`, `BGM.ADP`) | ignored for now: the menu plays the BIOS's own sounds |
| Menu Options: Serial SD warning (`[MISC]`) | ignored: Serial VMU is not in this branch yet |
| Theme folders, `FOLDRART.DAT` | ignored |

## Settings from the card: `[BIOSMENU]`

With **Settings > Other > Card defaults** on (the 1.7.1 "Honor Menu Defaults" byte, on by default), every key in this
section is applied at each start. Rows without a key keep what was saved. A change made on the console lasts until the next
start for a key the card sets, and for good otherwise. Turn Card defaults off to ignore the section.

Keys are the settings row names, and values the choices as they are shown, both in lower case with `_` between words
(`+` and `-` before a number become `plus` and `minus`). A choice's number works too (0 is the first). Wrong keys or values
are reported on the serial log as `OM cfg ...` and skipped.

```ini
[BIOSMENU]
game_order=sd_card_order
multi_disc_games=compact
online_features=on_auto_connect
online_time_sync=on_utc_plus_1
recently_played=last_20
```

| Key | Values |
|---|---|
| `game_order` | `a_z`, `sd_card_order` |
| `multi_disc_games` | `show_all_discs`, `compact` |
| `disc_sets_from` | `anywhere`, `same_folder_only` |
| `recently_played` | `off`, `last_10`, `last_20`, `last_30`, `last_40`, `last_50` |
| `remember_last_game` | `off`, `on` |
| `aspect_ratio` | `4_3`, `16_9` |
| `online_features` | `off`, `on_manual_connect`, `on_auto_connect` |
| `player_list_refresh` | `off`, `10_seconds`, `20_seconds`, `30_seconds`, `45_seconds`, `60_seconds` |
| `vmu_online_status` | `off`, `on` |
| `online_time_sync` | `off`, `on_utc_minus_12` ... `on_utc_plus_0` ... `on_utc_plus_14` (also `on_utc_plus_5_30` style) |
| `vmu_time_sync` | `off`, `on` |
| `vm2_game_id` | `send_to_all`, `send_to_first`, `off` |
| `boot_animation` | `full`, `license_only`, `animation_only`, `fast` |
| `exit_to_bios` | `standard`, `alternate`, `alternate_3d` |
| `clock_extender` | `off`, `on` |

The BIOS's own rows (language, sound, auto start, the clock) are kept in the console's flash and are not taken from the
card. New settings rows get a key automatically from their name.

The stock Card Manager has no editor for this section: write it by hand. It is kept when the Card Manager rewrites
`DEFAULTS.INI` from its Menu Options window.
