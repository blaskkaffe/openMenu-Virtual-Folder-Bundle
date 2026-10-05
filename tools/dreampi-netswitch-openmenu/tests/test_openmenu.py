"""The openMenu link module: openMenu's poll / game upload, the phone page's launch request, events and player matching."""
import json
import os
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from support import web, core, sandbox, cleanup

import netswitch_openmenu as om

UPLOAD = ("#openmenu-games 1 abc12345 3\n"
          "T1234N\t1\t1/1\tU\t\tSonic Adventure 2 (USA)\n"
          "MK51035\t2\t1/2\tU\tRacing\tCrazy Taxi\n"
          "bad line\n"
          "HDR-0001\t3\t1/1\tJ\t\tDeath\tCrypt\n")


class OpenMenu(unittest.TestCase):
    def setUp(self):
        self.tmp = sandbox()
        self.saved = om.GAMES_FILE, dict(om._state)
        om.GAMES_FILE = os.path.join(self.tmp, "openmenu_games.json")
        om._state.update({"seen": 0.0, "pending": None, "launched": None, "launched_time": 0.0, "games": None,
                          "events": [], "events_time": 1e18, "events_refreshing": False, "events_error": ""})
        self.srv = web.Server(("127.0.0.1", 0), web.Handler)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.base = "http://127.0.0.1:%d" % self.srv.server_address[1]
        web.refresh_page(force=True)

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        om.GAMES_FILE = self.saved[0]
        cleanup(self.tmp)

    def call(self, method, path, body=None, headers=None):
        req = Request(self.base + path, data=body, method=method, headers=headers or {})
        try:
            r = urlopen(req, timeout=10)
            return r.status, r.read().decode()
        except HTTPError as e:
            return e.code, e.read().decode()

    def upload(self):
        return self.call("POST", "/openmenu/games", UPLOAD.encode(), {"X-Requested-With": "openMenu"})

    def test_poll_asks_for_games_then_stops(self):
        status, text = self.call("GET", "/openmenu/poll?v=1&n=0&h=00000000")
        self.assertEqual(status, 200)
        self.assertEqual(text.splitlines(), ["openmenu 1", "NEED games"])
        self.assertEqual(self.upload()[0], 200)
        _, text = self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        self.assertEqual(text.splitlines(), ["openmenu 1"])
        _, text = self.call("GET", "/openmenu/poll?v=1&n=3&h=other")
        self.assertIn("NEED games", text)

    def test_upload_parsed_and_kept(self):
        self.upload()
        state = json.loads(self.call("GET", "/openmenu/state")[1])
        names = [g["name"] for g in state["games"]]
        self.assertEqual(names, ["Crazy Taxi", "Death", "Sonic Adventure 2 (USA)"])
        self.assertEqual(len(state["games"]), 3)
        self.assertTrue(os.path.exists(om.GAMES_FILE))
        om._state["games"] = None                       # a restart reads it back
        self.assertEqual(len(om.games()["games"]), 3)

    def test_bad_upload_refused(self):
        self.assertEqual(self.call("POST", "/openmenu/games", b"hello", {"X-Requested-With": "openMenu"})[0], 400)

    def test_upload_from_another_site_refused(self):
        self.assertEqual(self.call("POST", "/openmenu/games", UPLOAD.encode(), {"Origin": "http://evil.example"})[0], 403)

    def test_launch_needs_connected_dreamcast_and_known_game(self):
        self.upload()
        h = {"X-Requested-With": "netswitch"}
        self.assertEqual(self.call("POST", "/openmenu/launch", json.dumps({"product": "MK51035"}).encode(), h)[0], 409)   # not connected
        self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        self.assertEqual(self.call("POST", "/openmenu/launch", json.dumps({"product": "NOPE"}).encode(), h)[0], 409)     # not on the card
        self.assertEqual(self.call("POST", "/openmenu/launch", json.dumps({"product": "MK51035"}).encode(), h)[0], 200)

    def test_launch_delivered_once(self):
        self.upload()
        self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        self.call("POST", "/openmenu/launch", json.dumps({"product": "MK51035"}).encode(), {"X-Requested-With": "netswitch"})
        _, text = self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        self.assertEqual(text.splitlines(), ["openmenu 1", "LAUNCH MK51035"])
        _, text = self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        self.assertEqual(text.splitlines(), ["openmenu 1"])

    def test_old_launch_dropped(self):
        self.upload()
        self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        self.call("POST", "/openmenu/launch", json.dumps({"product": "MK51035"}).encode(), {"X-Requested-With": "netswitch"})
        om._state["pending_time"] -= om.LAUNCH_TTL + 1
        self.assertNotIn("LAUNCH", self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")[1])

    def test_launch_from_another_site_refused(self):
        self.assertEqual(self.call("POST", "/openmenu/launch", b'{"product":"MK51035"}', {"Origin": "http://evil.example"})[0], 403)

    def test_matching_players_to_games(self):
        glist = om.parse_games(UPLOAD)[1]
        self.assertEqual(om.match_game("Crazy Taxi", glist)["product"], "MK51035")
        self.assertEqual(om.match_game("sonic adventure 2", glist)["product"], "T1234N")
        self.assertIsNone(om.match_game("Quake III", glist))
        self.assertIsNone(om.match_game("", glist))

    def test_events_parsed(self):
        data = {"dcnet": {"online": True, "players": [], "events": [{"title": "Taxi night", "start": "2026-10-10 20:00", "game": "Crazy Taxi"}]},
                "events": [{"name": "Open lobby", "date": "2026-10-12"}, "just text"]}
        events = om.parse_events(data)
        self.assertEqual(sorted(e["title"] for e in events), ["Open lobby", "Taxi night", "just text"])
        self.assertEqual([e for e in events if e["title"] == "Taxi night"][0]["network"], "dcnet")
        self.assertEqual(om.parse_events({"unrelated": 1}), [])

    def test_events_get_game_ids(self):
        self.upload()
        om.fetch = lambda url: json.dumps({"events": [{"title": "Taxi night", "start": "x", "game": "Crazy Taxi"}]})
        om.refresh_events()
        state = json.loads(self.call("GET", "/openmenu/state")[1])
        self.assertEqual(state["events"][0]["product"], "MK51035")


if __name__ == "__main__":
    unittest.main()
