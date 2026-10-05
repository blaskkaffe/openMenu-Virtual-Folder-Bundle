# openMenu link: DreamPi Netswitch module

Drop-in module for [DreamPiAutoToggle](https://github.com/blaskkaffe/DreamPiAutoToggle). It pairs with this openMenu
build (`openMenu/src/openmenu/src/backend/dreampi_link.c`).

What it does
- openMenu uploads the SD card's game list (title, product ID, slot, disc, region, folder) to the Pi after a modem
  connection comes up. The page shows the list with a search box and a **Start** button per game.
- The page shows the online players (from the Online players module) and, for each one playing a game that is on the card,
  a **Join** button. It picks the game by matching the title to the card's list.
- Events are read from dc99.net (see below) and shown with a Start button when the event names a game on the card.
- Tapping Start/Join queues a launch. openMenu polls the Pi every 3 seconds, collects it, finds the game by product ID
  and starts it. Nothing is pushed to the Dreamcast.

Install
1. Copy `modules/openmenu/` into the add-on's `modules/` folder (next to `players/`, `numbers/`).
2. `sudo ./install.sh`. The module is on by default and can be switched in Settings > Modules.
3. On the Dreamcast, set **DC Now!** to **On (Auto-Connect)** (or connect by hand). The list appears on the page after
   the first poll.

Optional tests: copy `tests/test_openmenu.py` into the add-on's `tests/` and apply `tests/test_modules.patch`
(it adds the module to the module lists the tests keep).

Wire protocol (openMenu to Pi, over the PPP link, on the Pi's web port; the Pi's address is the DNS server the
Dreamcast was given)
- `GET /openmenu/poll?v=1&n=<games>&h=<hash>` answers `openmenu 1`, then `NEED games` if the Pi's copy does not match
  the hash, and `LAUNCH <product>` once when a launch is queued.
- `POST /openmenu/games` (with `X-Requested-With: openMenu`) body: `#openmenu-games 1 <hash> <count>`, then one game per
  line, tab separated: product, slot, disc, region, folder, name.

Limits and open points
- **Not run on a Dreamcast or a Pi.** The Pi side passes the add-on's test suite (305 tests, including 13 new ones). The openMenu
  side was only syntax-checked, not built or run.
- **Events:** DC99 has no events API. The module downloads `https://dc99.net/community/` (plain HTML, at most once every
  10 minutes while the page is open) and cuts out the `const EVENTS = [ ... ];` list the calendar draws from, then decodes it
  as JSON. It reads title (or name), date/start, end, location, summary, source and url from each entry. The field
  names are my best guess from your description: I could not open the page (the sandbox blocks dc99.net), so please check
  one real entry against `_event()` in `netswitch_openmenu.py`. If the page has no `EVENTS` list, the box says "no EVENTS list
  in the page (DC99 may have changed it)" and keeps the events it already had. Times are shown as DC99 gives them, without
  converting to a time zone. DC99 gives no game field, so an event gets a Start button only when a card game's name appears in
  its title or summary.
- **Anyone who can open the page can start a game.** Like the rest of the page, there are no accounts. A launch is
  refused from other sites and when the Dreamcast was not heard from in the last 15 seconds, and it expires after 60
  seconds, but it has no PIN. Keep the Pi on a trusted network.
- A launch only works while openMenu is the running program and the modem link is up. Once a game starts, openMenu hangs
  up and the game dials in itself, as before.
- Joining a game here only starts it. The game's own dialing and lobby still apply.
