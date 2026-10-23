# CLAUDE.md

Notes for working on this repo (openMenu virtual-folder bundle, "NewOpenMenu" BIOS-style menu). Work happens on
the `dev` branch; `main` is the release line. Commit and push to `dev` when a change is finished.

## Layout
- `openMenu/src/bios_engine/` portable engine (Ninja models, script VM, scene, game list `bios_list.c`, pages, CD player).
- `openMenu/src/bios_rom/` reads the BIOS ROM dump. `openMenu/src/openmenu/src/` the app (`ui_bios.c` screens, `gfx.c` PVR backend).
- Vision: `openMenu/PROJECT_BRIEF.md` (the owner's consolidated brief: ideas and roadmap, not a statement of what exists). `ROADMAP.md` checks it against the code.
- Docs: `openMenu/NEWMENU.md` (design, per screen), `ROADMAP.md` (project brief vs code), `TEST_CHECKLIST.md` (hardware results, the user's notes: do not overwrite their measurements).

## Preview without hardware
`bios_preview` is not in CMake; build by hand (needs `dc_boot.bin`, MD5 e10c53c2f8b90bab96ead2d368858623, from the user):
```
cd openMenu/src
gcc -O2 -o <out>/bios_preview bios_engine/tools/bios_preview.c bios_engine/src/*.c bios_rom/src/*.c -Ibios_engine/include -Ibios_rom/include -lm
<out>/bios_preview dc_boot.bin list.ppm 60 <selected row> -3     # game list; BLIST_SLOTS=n, BLIST_DISC=n
```
Engine test: same gcc line with `bios_engine/test/bios_engine_test.c`, run `./t dc_boot.bin`. The preview draws text as outline boxes and
has no right panel; real text/art only on the PVR path (hardware).

## Game list (current state, see NEWMENU.md "Game browser")
- One box, fixed height 73.5-350.5 px, 7 lines (`UIL_VISIBLE`, engine max 13). Corners keep the single-row radius (y-growth in `bios_scene`).
- Disc and lit pill only on the selected row. Multi-disc number = 3D digit models at the CD's bottom right (`disc_number`).
- Bottom row: Recent, Favorites (placeholder, no storage yet), Folders (top of tree), Sort (cycles A-Z / Year / SD order, `sf_sort`), Group (placeholder). Code in `ui_bios.c` (`button_text`, `cycle_sort`), order in `gd_list.c` `folder_cmp`. The app side cannot be compiled here (no KOS); it was checked only with a stub harness.
- `BVM_MAX_OBJECTS` is 128 (13 rows need it).

## Open items
- Hardware checks: title legibility at 7/9 rows, digits on a CRT, panel alignment with the box, fps, memory.
- Disc total ("2:4") is no longer shown anywhere; ask the user if it should come back.
- Brief says: favourite star and genre on the list rows, separate sort/group controls, X popup (cheats, disc, version, per-game network), Y screen text. Follow its button mappings and B hierarchy; mark hardware-dependent features as not working until firmware exists.
- See ROADMAP.md for the rest (favourites, genre, X popup, Y screen text, 60 fps).
