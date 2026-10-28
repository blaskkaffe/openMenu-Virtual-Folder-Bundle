# Test plan and todo (dev branch)

Everything below was written without a console build (the cloud session has no KallistiOS), so the first job is to make it compile.
`TEST_CHECKLIST.md` keeps your measurements and notes; this file is the order to work through and what to expect.
Mark a line `[x]` when it passes, `[-]` with a note when it half works.

## 0. Build

- [ ] `cmake --preset dc-release && cmake --build --preset dc-release` (see BUILD_INSTRUCTIONS.md). Expect compile fixes in the files changed
      without a compiler: `ui_bios.c`, `ui_internet.c`, `gfx.c`, `online.c`, `favorites.c`, `serial_vmu.c`, `vmu_files.c`, `ui_settings.c`,
      `ui_list.h`, `gd_list.c` (release-year sort), `dcnow_fetch.c`, `main.c`. New sources are in `src/openmenu/CMakeLists.txt`.
- [ ] Host tests still pass: `bios_engine_test` (needs dc_boot.bin, see CLAUDE.md), `test/clock_test.c`
      (`gcc -Isrc test/clock_test.c src/clock_math.c`).
- [ ] 1ST_READ.BIN boots to the BIOS-style main menu without a crash (serial log, `tools/serial_capture.py`).

## 1. Disc / card contents and settings to prepare

- [ ] Menu disc has `NOWO.PVR`, `NOWB.PVR` and `CNET.PVR` next to `LOGO.PVR` (the Card Manager's `tools/openMenu/menu_data` folder has them), `EMPTY.VMU`,
      `META.DAT`, `ICON.DAT`, `BOX.DAT`, `DISC.DAT`.
- [ ] A card with: a folder tree, 10+ games, at least one multi-disc game (Compact mode), one PSX disc, a game without art, a browser disc,
      homebrew (type "other").
- [ ] Settings used below: Game list > Game order / Serial VMU / Recently played; Settings > Internet > Online features On or Auto-connect,
      Internet screen > Settings > Online time sync (a zone), Player list refresh; Display > Clock format.
- [ ] Test on an NTSC-U console (orange swirl) and, if you can, a PAL one (blue swirl, region byte 2). Note the console and BIOS version.

## 2. Game list (the single box)

- [ ] One tall box, 7 lines, same width as before, clear of the BACK marker and the five buttons; corners the same radius as a single row,
      left edge straight (no 1 px step).
- [ ] Only the selected line has the lit pill and a spinning disc; other lines have none. Disc shows the game's own label, else ICON.DAT art,
      else the blue (PAL) / red (NTSC) BIOS disc.
- [ ] Multi-disc game: the disc number appears as 3D gold digits at the CD's bottom right (also 10+), Left/Right changes it, A starts that disc.
      No pill or "2:4" at the end of the row; titles use the full width.
- [ ] Long title scrolls on the selected line; titles readable at 37 px spacing (try `UIL_VISIBLE` 9 as well).
- [ ] Right panel lines up with the box top and bottom; case, info, art fit the shorter panel.
- [ ] Scrolling fast (hold Down) with no stutter; fps / memory acceptable (the object limit is now 128). Write the numbers in TEST_CHECKLIST.md.
- [ ] Letter jump on a keyboard, mouse hover/click on rows, buttons and BACK, B hierarchy: list -> bottom row -> main menu.

## 3. Bottom row

- [ ] Five buttons left to right: Recent, Favorites, Folders, Sort, Group; labels fit (long form or short), X or Right from BACK reaches them.
- [ ] Recent opens the recently played popup (A starts, B closes).
- [ ] Folders jumps to the top of the folder tree from a subfolder; with no folders says so.
- [ ] Sort: A cycles A-Z, Year, SD order; the label shows the active one; the list reorders in the same folder with the cursor on the same game;
      Settings > Game order shows the same value after the change; the order survives a restart.
- [ ] Year order: oldest first by the disc's IP.BIN date, games without a date last.
- [ ] Group says "No other groups yet".

## 4. Favorites (own file, not OPENMENU.SYS)

- [ ] X on a game > Favorite toggles On/Off (click sound); the title gets "* " in the list (check the BIOS font has the star character).
- [ ] Favorites button lists the starred games, newest first; A starts one; Y removes one; B closes; empty list says so.
- [ ] Restart: the stars are still there. Test: VMU only (no SD), SD only, both. `EXTENDED.SYS` shows in the BIOS File screen as 3 blocks with a
      gold icon, `/sd/OPENMENU/EXTENDED.SYS` exists with an SD card.
- [ ] No card and no SD: it still works until power off and says "Could not save favorites"; 101st star says the list is full.
- [ ] Swap to Derek's 1.7.1 menu and back: nothing else on the VMU is damaged, settings kept.

## 5. Serial VMU (needs a serial SD adapter)

- [ ] Settings > Game list > Serial VMU: choose the socket (A1..D2). Off = nothing below happens.
- [ ] A game with a serial and no image on the card: popup with blank VMU / launch as is / launch without / cancel. "Blank" asks to erase first
      and needs EMPTY.VMU.
- [ ] With an image (made by this menu or 1.7.1): 256 blocks written (about 4 s, progress shown), game starts, `OPENMENU/LASTDISC.TXT` holds
      `<serial>-1`, `TITLE.TXT` is next to `SLOT1.VMU`.
- [ ] After the game, next start: "Backing up the VMU", data in `SLOT1.VMU`, LASTDISC.TXT gone. Then swap with 1.7.1's menu: it reads the same files.
- [ ] Damaged file, no SD adapter, no VMU in the socket: the right question each time; Skip keeps LASTDISC.TXT and the next restore warns that the
      last game's VMU is not backed up.
- [ ] Homebrew (type other) and games without a serial start with none of this.
- [ ] Games started from the Recent / Favorites popups and from Internet > Online go through the same restore.

## 6. Main menu top bar and header logo

- [ ] Clock: Settings > Display > Clock format: 12 hour (10:27 PM), 24 hour, Date only, 12 hour + .beat, 24 hour + .beat; the header changes within a
      second. .beat with Online time sync Off shows the plain clock. Check one .beat value against a Swatch clock (BMT = UTC+1).
      Note: the default is 12 hour (the saved default), so the header reads AM/PM unless changed; decide if that is wanted.
- [ ] Above the clock, same font: a lamp (grey off, yellow connecting, green online, red failed) and, once the list is in, "DCNow! n DCNet n".
      Nothing with Online features Off. Right-aligned to the clock, not clipped with 3-digit counts.
- [ ] Logo, offline: the normal BIOS logo (PAL console: blue swirl). With your own LOGO.PVR nothing changes in any state.
- [ ] While connecting the logo stays the normal one (no turning swirl).
- [ ] Online on Dreamcast Now!: "Now!" in the swirl's colour right after "Dreamcast" (not overlapping the clock, not cut off).
- [ ] Online through a DreamPi with DCNET selected: the swirl with "DCNET" over the wordmark's place, same size and baseline as "Dreamcast".
      "Now!" shows right away, also without the DreamPi add-on; with it the logo changes to "DCNET" when the add-on reports that network.
- [ ] Without NOWO/NOWB/CNET.PVR on the disc the logo stays as it is online (no crash).
- [ ] Compare the letters with the real wordmark at 1:1 on a CRT: weight, height, baseline (known: baseline about 1 px high, strokes lighter than
      the wordmark's own).

## 7. Internet screen

- [ ] Music icon reads Internet only with Online features on; opens the screen; five buttons: Online, Play, Events, News, Settings.
- [ ] Connection: modem dials (X), serial cable to DreamPi 2.0+, BBA/LAN adapter connects on opening the screen, W5500 (not supported by the
      engine: expected "none found"). Auto-connect dials at boot after the menu is up. X again hangs up; the 5 s cooldown; pulling the cable shows
      "Connection lost.". Write down the exact status text of every failure (reason on the status line, advice at the bottom).
- [ ] Online page: Players left, Games right; Y switches column; heading turns yellow; A on a game that is on the card (*) starts it; footer shows the
      selected player's game. With the fetch failing the list shows the step or the error, A retries (record the text: this is the "no players" bug).
- [ ] Play page: browser discs start with A; catalogue row says coming soon. Events: upcoming DC99 events (DreamPi add-on, else dc99.net).
      News: placeholder. Settings: A on Connection connects/hangs up; the other rows say coming soon.
- [ ] VMU online status, Online time sync, phone page launch (DreamPi link) still work as on the main branch; launching a game while online hangs up first.

## 8. Compatibility and regressions

- [ ] Settings saved here read fine by Derek's 1.7.1 with nothing changed (also with the Swatch .beat clock on: that flag is in EXTENDED.SYS, not in OPENMENU.SYS) and the other way round. A save from an older build of this menu with Clock format 3 or 4 becomes 12 / 24 hour + .beat at the first start.
- [ ] Launch: Dreamcast game, multi-disc, PSX disc (Bleem!/Bloom), CodeBreaker, browsers, homebrew; exit from a game back to the menu.
- [ ] Settings layout: one tall box with 5 rows (same back as the game list, as wide as the old rows, icons and text at the original size), a separate information bar at the bottom where the original help box is (no coloured square, help text starting at the value column, wrapped on a second line, nothing outside the bar), green arrows (not ^ / v text) when rows are above / below and a green scroll bar thumb that follows the cursor between them; box corners as the original (large left, small right; info box small all round), distance to BACK as the original; no label runs into its pill, pills line up with the rows, icons fit, BACK marker not covered, value column does not collide with the label.
- [ ] Settings: groups in order BIOS, Clock and date, Display, Game list, Starting games, System, Internet, Other; help box shows group and n/total; Display > Background changes the gradient (6 choices, BIOS blue = as before), Dark mode dims it, both survive a restart and a swap to 1.7.1 and back; judge the colours on a CRT/TV. Internet screen > Settings: the three rows cycle with A and persist.
- [ ] Files screen, Settings, date and time editor, About, debug log page, performance test still work.
- [ ] Memory and stability: browse 500+ games, open every screen twice, leave it on the Internet screen for an hour online.

## Todo (not done yet)

Fix first
1. Compile and run everything above; report compile errors and the online failure texts.
2. Online "never connects" / "no players": cause still unknown. Needs the status text; candidate causes: plain-HTTP feed (dreamcast.online may need
   HTTPS), serial probe before the modem dial, G2-bus probe timing. W5500 has no engine at all (neither here nor on main).
3. Logo letters: baseline 1 px high and strokes lighter than the wordmark's own; decide the weight (match the typeface or the wordmark).
4. Decide: clock default 12 vs 24 hour; bring back the disc total ("2:4")?; Settings "Offline" ordering of the clock choices.

Features still open (see ROADMAP.md)
- Keyboard/mouse indicator on the icons of the File screen (next); VMU slot / GameID box, controller type label, Bluetooth pairing (firmware-bound).
- Favorites as a Group, genre/developer/publisher/year groups (needs META2.DAT), favourite star and genre on the list rows, version grouping.
- X popup: cheats, version picker, per-game network (DCNow!/DCNET, own storage file); Y screen: description text, scroll, snap/flip.
- Internet: Play catalogue of online games, News and Events from the DreamPi companion, notifications for starred players/games/events, star
  players/games, ISP settings editor, DreamPi Ethernet/Wi-Fi settings, interface tests (modem, BBA, serial, W5500), written companion protocol.
- Serial VMU: the fallback VMU picker, exit to BIOS, a File-menu Serial VMU submenu, manual backup/load.
- Settings: background colour choice and dark mode, move the online rows to the Internet menu; flash partition compaction before the ISP editor.
- Y2K-style look for the Internet pages; 60 fps (still below on hardware).

## Stutter at start and random frame drops

Every frame longer than 1.5 refreshes is recorded from the first frame on (`dbglog_hitch`, no debug overlay needed). Open the debug log page
(QR code, also sent over serial as `OM log`) and read the `HITCH` lines (the first 12 of 48 kept; `HITCH n total` is the count):
`frame|ms|period|logic|build|wait|art|art_us|text|screen` in microseconds. Reading them:
- many hitches in the first 5 s, `art_us` or `text` high: pictures and text built on first use (preload or spread them);
- `logic` high: Auto-Connect, list build, VMU/SD access on the main thread (move to a thread or spread);
- `build` high: too much drawn that frame; `wait` high with low build: the GPU/TA is the limit (vertices, translucent overdraw);
- all small but period long: something outside the loop (disc access, interrupts, a thread holding the CPU).
Take one log right after boot (without touching anything), one after a minute on the main menu, one after scrolling the game list, and the same
with Online off, to see which feature adds hitches.

## Freeze when leaving a menu with B (also seen in Flycast)

Leaving Settings prints `OM leave settings: save / list / labels / done` (Flycast log or serial). The last line before the freeze is the step that hangs
(`uis_commit` = saving to SD/VMU, `list` = rebuilding and sorting the game list, `labels`). Note which menu you left, and whether the freeze
also happens from Files, Internet, Music, the game list and the main menu.

Freeze on B in every menu: B is the only button that plays the CANCEL sound (id 2) and returns to the main menu. Log lines `OM B: pressed, frame n, screen s`
then `OM B: frame n+1 after B ok` show whether the freeze is inside the B handling (second line missing) or later (second line there). A test
build with `-DOM_MUTE_CANCEL` (sound.c) leaves out the CANCEL sound: if the freeze is gone, it comes from the sound driver command.

## Todo: header logo without a DreamPi (Flycast, BBA, LAN)

Open: Flycast uses DCNET, but the player lists are DCNow!'s, and with no DreamPi nothing tells the menu which network it is on
(`dreampi_link_network()` is 0). Decision so far: "Now!" is the default whenever the network is not known. Figure out whether DCNET can be
recognised without a DreamPi (a setting Auto / DCNow! / DCNET, or a test of the connection). Also: in Flycast no logo change is visible at all.
Check the log: `OM logo: NOWO n NOWB n CNET n` (are the three .PVR files on the disc image?) and `OM logo: mode m (shown, state, network, ...)`
(state must be 2 = online for mode 1; the setting Online features must not be Off).

Freeze on B while connecting or loading online data (not offline): the main screen is drawn with a logo mode then (1 = Now!), code
that is new on this branch. A test build with `-DOM_NO_LOGO_MODES` keeps the logo plain. If the freeze is gone with it, the logo code (gfx.c:
build_logo_variants / gfx_logo_badge) is the cause; if not, the cause is in the connection or fetch threads (the `OM B:` and `OM logo:` lines say which).

## Serial VMU Slots (Settings > Game list > Serial VMU Slots: On)
- [ ] First start of a game with no image in any slot: no slot question, the usual "No Serial VMU for this game yet" choices (slot 1).
- [ ] Next start (after the backup): the question "Serial VMU slot" lists Slot 1..5 with the time of the image (EMPTY when none), or the first line of
      `SLOT<n>.TXT` as the label; Launch without Serial VMU; Cancel (B).
- [ ] An empty slot chosen: the blank/as-is choices; a filled slot: the image is restored and the game starts.
- [ ] At the next start the VMU is backed up into the slot the game was started from (no question; 1.7.1 asks, this menu does not).
      `LASTDISC.TXT` holds `<serial>-<slot>`; `SLOT<n>.VMU` has the new time. Check on a card shared with 1.7.1 that it reads the same files.
- [ ] A backup left undone (skipped) is offered ("Back it up now") before the next restore, also when it was made from slot 3 to 5.

## Freeze on B (findings so far)
- Seen: B on the Events page while it was loading and nothing shown yet; B out of the case viewer and the game list right after changing the game
  or turning the case. The viewer's B does nothing but `sound_sfx(BAUDIO_SFX_CANCEL)`, so the cancel sound is the main suspect (test: `-DOM_MUTE_CANCEL`).
- Done in code: leaving the Internet screen no longer waits for the events/news worker threads (`*_fetch_abort` only raises the flag; the poll joins
  the thread when it has ended; a start during that time is queued).
- Test: B at once on Events / News while loading, then open them again (must load); B out of the case viewer right after turning it, with and
  without `-DOM_MUTE_CANCEL`.

## Dark mode, logo and start-up (this round)
- [ ] Settings > Display > Dark mode On: the header bar is dark grey, the clock and the player counts white, the "Dreamcast" wordmark light grey (swirl unchanged),
      the waves of the background dimmed and in the hue of the chosen background colour (Green: green waves; BIOS blue: blue). Off: everything as the BIOS.
- [ ] Dark mode with Dreamcast Now! / DCNET online: "Now!" and "CNET" readable on the dark bar (NOWOD/NOWBD/CNETD.PVR on the disc; without them the logo is left alone).
- [ ] A LOGO.PVR of your own: left as it is in dark mode too (the bar is still dark).
- [ ] No turning swirl while connecting: the logo stays plain until online.
- [ ] Start-up with Auto-Connect: the stutter in the first seconds should be less (connection threads run below the menu). Compare the `HITCH` count right after boot with Online features Off and On.
