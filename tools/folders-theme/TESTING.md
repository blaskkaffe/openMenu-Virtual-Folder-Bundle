# Test list for this build

None of this has been run on a Dreamcast by the person who wrote it: it was written without a toolchain. Please report anything that
fails, with a photo.

## 1. Build
- [ ] It builds with no errors (new files: `dreampi_link.c`, `online_games.c`, `backdrop_texture.h`).

## 2. Themes (Settings -> Theme: Folders, Region/Theme: FOLDERS_8 or FOLDERS_9)
Install `out_animated/` OR `out/` into `theme/FOLDERS_8` and `FOLDERS_9` on the card (the same slots, one set at a time).
- [ ] `out/` (still): a picture of the waves sits behind the boxes, the legend shows the *default theme's* round A/B/X/Y buttons and
      the Start triangle, X reads "Extras". Works the same on stock openMenu.
- [ ] `out_animated/`: the waves move, the sky is steady, no missing borders, no vanishing text, no square holes in the picture.
- [ ] Orange and blue sets both load (8 = orange, 9 = blue).
- [ ] The panel borders change colour when the network changes (see 4).

## 3. Corners
- [ ] Bottom-right "single disc" details bar: both right-hand corners are round and match the left ones (the radius is now
      clamped to half the bar's height).
- [ ] Settings popup, Extras popup, disc options: all four corners round, no notch near the title ("Settings").
      If a notch is still there, send a photo and say which theme and which menu.
- [ ] Settings popup, scroll through all rows: no blank box between row 4 and 5 (the legend/disc pieces of the picture are no
      longer drawn behind a popup). Also try with a plain (non-backdrop) theme.

## 4. Extras menu (was "Use Cheats", press X in Folders)
- [ ] The title says "Extras". Options: Online Status, Use DC Now!, Use DCNET, (Launch with CodeBreaker only if PELICAN.BIN exists), Close.
- [ ] Online Status with DC Now! off in Settings: a message says to turn it on. With it on: the player/game window opens.
- [ ] In that window, "View" shows games; a row marked `>` is on the card. A on it starts the game.
- [ ] Use DCNET / Use DC Now! (needs the DreamPi add-on and a connection): the line under the options says sending / done / failed /
      no link. On the Pi the network switches, and the panel border colour follows after the next poll.
- [ ] No DreamPi add-on: the buttons say "no link" and nothing hangs.
- [ ] Launch with CodeBreaker still works.

## 5. DreamPi link (needs the add-on with the openMenu module)
- [ ] Auto-connect dial starts as soon as openMenu starts, before/with the loading screen.
- [ ] After leaving a game (face buttons + Start) it reconnects and the link comes back.
- [ ] Game list is uploaded (the phone page shows the games on the card).
- [ ] Tapping a game in the phone page starts that game.
- [ ] Games being played online show in green with a small phone icon in the list (Folders), and the player counts are right.
- [ ] Event reminder: a banner shows for a few seconds with source and title.
- [ ] DC Now! window: Show filter (both / DCNow! / DCNET) works; with the Pi connected the list comes from the Pi.

## 6. Nothing else changed
- [ ] LineDesc, Grid3 and Scroll look and behave as before.
- [ ] Settings still saves, Exit to BIOS, multi-disc popups still work.
- [ ] A theme without `backdrop=1` or `menu_corner_radius` looks exactly like stock.
