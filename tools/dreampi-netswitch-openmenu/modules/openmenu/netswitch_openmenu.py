# DreamPi Netswitch add-on - OPTIONAL "openMenu link" module.
# openMenu (the Dreamcast menu) asks this page's service two things over the PPP link, and the page shows the result:
#   GET  /openmenu/poll?v=1&n=<games>&h=<hash>   openMenu's heartbeat. Answers "openmenu 1", then "NEED games" when the Pi
#                                                 has no copy of the SD card's game list, and "LAUNCH <product>" once when
#                                                 someone picked a game on the phone page.
#   POST /openmenu/games                          the game list: "#openmenu-games 1 <hash> <count>" then one game per line,
#                                                 tab separated: product, slot, disc, region, folder, name.
# The phone page uses:
#   GET  /openmenu/state                          games, players matched to games, events, and whether the Dreamcast is here.
#   POST /openmenu/launch  {"product": "..."}     asks openMenu to start that game the next time it polls.
# Nothing is pushed to the Dreamcast: a launch waits until openMenu asks. Works on Python 3 and 2.7.
import json
import os
import re
import threading
import time

try:
    from urllib.request import urlopen, Request
except ImportError:   # Python 2.7
    from urllib2 import urlopen, Request

import netswitch_core as core

GAMES_FILE = os.path.join(core.BASE_DIR, "openmenu_games.json")
SEEN_WINDOW = 15          # seconds: openMenu polls every 3, so it is "connected" while it was heard this recently
LAUNCH_TTL = 60           # a launch nobody collected within this time is dropped
EVENTS_CACHE = 600
MAX_BODY = 2000000
MAX_GAMES = 5000
MAX_EVENTS = 30
TIMEOUT = 8
MAX_PAGE_BYTES = 1000000

_lock = threading.Lock()
_state = {"seen": 0.0, "pending": None, "pending_time": 0.0, "launched": None, "launched_time": 0.0,
          "games": None, "events": [], "events_time": 0.0, "events_refreshing": False, "events_error": ""}


# ------------------------------------------------------------------ game list
def _clean(text, limit):
    return re.sub(r"[\x00-\x1f]", " ", text).strip()[:limit]


def parse_games(text):
    """(hash, games) from openMenu's upload, or None when it isn't one."""
    lines = text.splitlines()
    if not lines or not lines[0].startswith("#openmenu-games 1 "):
        return None
    parts = lines[0].split()
    if len(parts) < 3:
        return None
    games, seen = [], set()
    for line in lines[1:]:
        f = line.split("\t")
        if len(f) < 6 or not f[0].strip():
            continue
        product = _clean(f[0], 11)
        if not re.match(r"^[A-Za-z0-9_-]+$", product):
            continue
        try:
            slot = int(f[1])
        except ValueError:
            slot = 0
        key = (product, f[2])
        if key in seen:
            continue
        seen.add(key)
        games.append({"product": product, "slot": slot, "disc": _clean(f[2], 7), "region": _clean(f[3], 3),
                      "folder": _clean(f[4], 120), "name": _clean(f[5], 100)})
        if len(games) >= MAX_GAMES:
            break
    games.sort(key=lambda g: (g["name"].lower(), g["disc"]))
    return parts[2], games


def _load_games():
    try:
        with open(GAMES_FILE) as f:
            data = json.load(f)
        if isinstance(data, dict) and isinstance(data.get("games"), list):
            return data
    except (IOError, OSError, ValueError):
        pass
    return {"hash": "", "time": 0, "games": []}


def games():
    with _lock:
        if _state["games"] is None:
            _state["games"] = _load_games()
        return _state["games"]


def save_games(h, glist):
    data = {"hash": h, "time": int(time.time()), "games": glist}
    tmp = GAMES_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f)
    os.rename(tmp, GAMES_FILE)
    with _lock:
        _state["games"] = data


# ------------------------------------------------------------------ matching a game title to the card
# (the page matches the online players to the card the same way, from the Online players module's /players)
def _norm(text):
    return re.sub(r"[^a-z0-9]+", "", (text or "").lower())


def match_game(title, glist):
    """The game on the card that a player's game title means, or None. Exact normalised name first, then
    one name containing the other (so "Sonic Adventure 2" finds "Sonic Adventure 2 (USA)"); the shortest
    containing name wins so a plain title beats a longer one."""
    want = _norm(title)
    if len(want) < 3:
        return None
    exact = [g for g in glist if _norm(g["name"]) == want]
    if exact:
        return exact[0]
    loose = [g for g in glist if len(_norm(g["name"])) >= 3 and (want in _norm(g["name"]) or _norm(g["name"]) in want)]
    loose.sort(key=lambda g: len(_norm(g["name"])))
    return loose[0] if loose else None


# ------------------------------------------------------------------ events (from dc99.net/community)
# DC99 has no events API. Its community page is plain HTML with a script holding the list the calendar draws from:
#   const EVENTS = [ {...}, {...} ];
# so the page is downloaded and that JSON list is cut out and decoded (nothing is scraped from the drawn calendar).
EVENTS_URL = "https://dc99.net/community/"
_START_KEYS = ("start", "starts", "start_time", "starts_at", "start_date", "date", "datetime", "when", "time")
_END_KEYS = ("end", "ends", "end_time", "ends_at", "end_date", "end_date_time")
_TITLE_KEYS = ("title", "name", "event", "event_name")
_TEXT_KEYS = ("summary", "description", "details", "info", "text")
_PLACE_KEYS = ("location", "place", "where", "venue")
_URL_KEYS = ("url", "link", "href")
_SRC_KEYS = ("source", "origin")

try:
    _TEXT = basestring  # noqa: F821
except NameError:
    _TEXT = str


def _pick(item, keys):
    for k in keys:
        v = item.get(k)
        if isinstance(v, (_TEXT, int, float)) and not isinstance(v, bool) and str(v).strip():
            return str(v).strip()
    return ""


def extract_events(html):
    """The decoded EVENTS list of the community page, or None when the page has no such list (DC99 changed it)."""
    m = re.search(r"\bEVENTS\s*=\s*\[", html)
    if not m:
        return None
    try:
        data, _ = json.JSONDecoder().raw_decode(html[m.end() - 1:])
    except ValueError:
        return None
    return data if isinstance(data, list) else None


def _event(item):
    if not isinstance(item, dict):
        return None
    title = _pick(item, _TITLE_KEYS)
    if not title:
        return None
    url = _pick(item, _URL_KEYS)
    if url.startswith("/"):
        url = "https://dc99.net" + url
    if not url.startswith(("http://", "https://")):
        url = ""
    return {"title": title[:100], "start": _pick(item, _START_KEYS)[:40], "end": _pick(item, _END_KEYS)[:40],
            "location": _pick(item, _PLACE_KEYS)[:80], "text": _pick(item, _TEXT_KEYS)[:300],
            "source": _pick(item, _SRC_KEYS)[:20], "url": url, "product": ""}


def parse_events(html):
    """(events, error): the events of the community page, soonest first."""
    data = extract_events(html)
    if data is None:
        return [], "no EVENTS list in the page (DC99 may have changed it)"
    events = [e for e in (_event(i) for i in data) if e]
    events.sort(key=lambda e: e["start"])
    return events[:MAX_EVENTS], ""


def match_in_text(text, glist):
    """The card game whose name appears inside an event's title or summary (the longest name wins), or None."""
    hay = _norm(text)
    best = None
    for g in glist:
        n = _norm(g["name"])
        if len(n) >= 5 and n in hay and (best is None or len(n) > len(_norm(best["name"]))):
            best = g
    return best


def fetch(url):
    """Page text of a URL. Replaced by the tests."""
    req = Request(url, headers={"User-Agent": "Mozilla/5.0 (compatible; dreampi-netswitch)", "Accept": "text/html, */*"})
    return urlopen(req, timeout=TIMEOUT).read(MAX_PAGE_BYTES).decode("utf-8", "replace")


def refresh_events():
    with _lock:
        if _state["events_refreshing"]:
            return
        _state["events_refreshing"] = True
    events, error = [], ""
    try:
        try:
            events, error = parse_events(fetch(EVENTS_URL))
        except Exception as e:
            error = str(getattr(e, "reason", None) or e)[:80]
        glist = games()["games"]
        for e in events:
            g = match_in_text(e["title"] + " " + e["text"], glist)
            if g:
                e["product"] = g["product"]
    finally:
        with _lock:
            if events or not _state["events"]:      # a failed fetch keeps the events already known
                _state["events"] = events[:MAX_EVENTS]
            _state.update({"events_time": time.time(), "events_refreshing": False,
                           "events_error": error if not _state["events"] else ""})


def current_events():
    with _lock:
        stale = time.time() - _state["events_time"] > EVENTS_CACHE and not _state["events_refreshing"]
        out = list(_state["events"])
        error = _state["events_error"]
    if stale:
        t = threading.Thread(target=refresh_events)
        t.daemon = True
        t.start()
    return out, error


# ------------------------------------------------------------------ the launch handshake
def poll_reply(headers_query):
    """What openMenu is told. headers_query: the query string of GET /openmenu/poll."""
    q = dict(p.split("=", 1) for p in headers_query.split("&") if "=" in p)
    now = time.time()
    lines = ["openmenu 1"]
    with _lock:
        _state["seen"] = now
        if _state["pending"] and now - _state["pending_time"] > LAUNCH_TTL:
            _state["pending"] = None
        if _state["pending"]:
            lines.append("LAUNCH " + _state["pending"])
            _state["launched"], _state["launched_time"] = _state["pending"], now
            _state["pending"] = None
    have = games()
    if q.get("h", "") != have.get("hash", "") or not have["games"]:
        lines.append("NEED games")
    return "\n".join(lines) + "\n"


def request_launch(product):
    """(ok, message). The product must be on the card and openMenu must have been heard lately."""
    if not any(g["product"] == product for g in games()["games"]):
        return False, "That game is not on the card's list."
    with _lock:
        if time.time() - _state["seen"] > SEEN_WINDOW:
            return False, "The Dreamcast is not connected to openMenu right now."
        _state["pending"], _state["pending_time"] = product, time.time()
    core.debug_log("openMenu link: launch %s requested" % product)
    return True, "Sent. The Dreamcast starts it within a few seconds."


def state():
    have = games()
    now = time.time()
    events, error = current_events()
    with _lock:
        seen_ago = int(now - _state["seen"]) if _state["seen"] else None
        pending = _state["pending"] if _state["pending"] and now - _state["pending_time"] <= LAUNCH_TTL else None
        launched = _state["launched"] if now - _state["launched_time"] <= 30 else None
    return {"connected": seen_ago is not None and seen_ago <= SEEN_WINDOW, "seen_ago": seen_ago,
            "pending": pending, "launched": launched, "games": have["games"], "games_time": have.get("time", 0),
            "events": events, "events_error": error}


# ---------------------------------------------------------------- the web service's side
def _poll(h):
    query = h.path.split("?", 1)[1] if "?" in h.path else ""
    h.send(poll_reply(query), "text/plain; charset=utf-8")


def _state_get(h):
    h.send(json.dumps(state()), "application/json")


def _games_post(h):
    raw = h._body(MAX_BODY)
    parsed = parse_games(raw.decode("utf-8", "replace"))
    if parsed is None:
        h.send("bad game list\n", "text/plain; charset=utf-8", status=400)
        return True
    save_games(parsed[0], parsed[1])
    core.debug_log("openMenu link: %d games received" % len(parsed[1]))
    h.send("ok\n", "text/plain; charset=utf-8")
    return True


def _launch_post(h):
    try:
        product = str(json.loads(h._body(2000).decode("utf-8", "replace")).get("product", ""))
    except (ValueError, AttributeError):
        product = ""
    ok, message = request_launch(product)
    h.send(json.dumps({"ok": ok, "message": message}), "application/json", status=200 if ok else 409)
    return True


GET = {"/openmenu/poll": _poll, "/openmenu/state": _state_get}
POST = {"/openmenu/games": _games_post, "/openmenu/launch": _launch_post}
