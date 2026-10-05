# openMenu link: DreamPi Netswitch module

Drop-in module for [DreamPiAutoToggle](https://github.com/blaskkaffe/DreamPiAutoToggle), written for the **`development`
branch** (page kit 2: `layout.json`, widgets, picker order). It pairs with this openMenu build
(`openMenu/src/openmenu/src/backend/dreampi_link.c`).

What it does
- openMenu uploads the SD card's game list (title, product ID, slot, disc, region, folder) to the Pi after a modem
  connection comes up.
- The dashboard box **Dreamcast (openMenu)** shows whether openMenu is connected and how many games are on the card. Opened,
  it lists the card's games with a search box and a **Start** button each, and, for every online player whose game is on the
  card, a **Join** button. Players come from the Online players module's data, matched to the card by title.
- Tapping Start or Join (after a confirmation) queues a launch. openMenu polls the Pi every 3 seconds, collects it, finds the
  game by product ID and starts it. Nothing is pushed to the Dreamcast.
- **Events are not in this module.** The events module on the branch owns them (and `GET /api/events/upcoming`).

Install
1. Copy `modules/openmenu/` into the add-on's `modules/` folder (next to `players/`, `events/`).
2. `sudo ./install.sh`. The module is on by default and can be moved or switched in Settings > Modules.
3. On the Dreamcast, set **DC Now!** to **On (Auto-Connect)** (or connect by hand). The list appears after the first poll.

Files
- `module.json`, `layout.json`: the box (an `infobox` over the data source `GET /openmenu/view`, a colour pick in Appearance).
- `page.js`: one custom widget, `openmenu-games` (search, Start, Join), since no standard widget has a search and per-row actions.
  It uses the kit's own classes and ships no CSS.
- `netswitch_openmenu.py`: the web entry. It imports no other module (the layering test).

Tests (optional)
- Copy `tests/test_openmenu.py` into the add-on's `tests/` and apply `tests/test_modules.patch` (adds the module to the lists
  that test keeps). With both, the add-on's suite passes: 548 tests on `development` at `b7aaa46`, 9 of them new.
- `tests/ui/openmenu.js` is a browser check like the other files in `tests/ui/`: start the demo server with `FAKEPLAYERS=1`,
  then `NODE_PATH=/opt/node22/lib/node_modules PORT=<port> node openmenu.js`. It uploads a game list, checks the box, the
  search, a Join for a fake player and a queued launch (10 checks, all passing).

Wire protocol (openMenu to Pi, over the PPP link, on the Pi's web port; the Pi's address is the DNS server the Dreamcast was
given)
- `GET /openmenu/poll?v=1&n=<games>&h=<hash>` answers `openmenu 1`, then `NEED games` if the Pi's copy does not match the
  hash, and `LAUNCH <product>` once when a launch is queued.
- `POST /openmenu/games` (with `X-Requested-With: openMenu`) body: `#openmenu-games 1 <hash> <count>`, then one game per
  line, tab separated: product, slot, disc, region, folder, name.
- For the page: `GET /openmenu/view` (small, polled every 5 s), `GET /openmenu/games` (read again when the hash changes) and
  `POST /openmenu/launch {"product": "..."}`.

Limits and open points
- **Not run on a Dreamcast or a Pi.** The Pi side passes the add-on's test suite and a headless-browser check; the openMenu side
  was only syntax-checked, not built or run.
- **Anyone who can open the page can start a game.** Like the rest of the page, there are no accounts. A launch is refused from
  other sites and when openMenu was not heard from in the last 15 seconds, and it expires after 60 seconds, but it has no PIN.
  Keep the Pi on a trusted network.
- A launch only works while openMenu is the running program and the modem link is up. Once a game starts, openMenu hangs up and
  the game dials in itself, as before.
- Joining a game here only starts it. The game's own dialing and lobby still apply.
- Players are matched to games by title (the same name, or one name inside the other). A game whose title differs a lot has no
  Join button.
