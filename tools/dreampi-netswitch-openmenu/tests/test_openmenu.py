"""The openMenu link module: openMenu's poll / game upload and the phone page's launch request and views."""
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
        om._state.update({"seen": 0.0, "pending": None, "launched": None, "launched_time": 0.0, "games": None})
        self.srv = web.Server(("127.0.0.1", 0), web.Handler)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.base = "http://127.0.0.1:%d" % self.srv.server_address[1]
        web.refresh_page(force=True)

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
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
        state = json.loads(self.call("GET", "/openmenu/games")[1])
        self.assertEqual(state["hash"], "abc12345")
        names = [g["name"] for g in state["games"]]
        self.assertEqual(names, ["Crazy Taxi", "Death", "Sonic Adventure 2 (USA)"])
        self.assertEqual(len(state["games"]), 3)
        self.assertTrue(os.path.exists(om.games_file()))
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

    def test_view_says_what_the_box_shows(self):
        view = json.loads(self.call("GET", "/openmenu/view")[1])
        self.assertEqual((view["title"], view["connected"], view["count"]), ("Not seen yet", False, 0))
        self.assertIn("No game list yet", view["note"])
        self.upload()
        self.call("GET", "/openmenu/poll?v=1&n=3&h=abc12345")
        view = json.loads(self.call("GET", "/openmenu/view")[1])
        self.assertEqual((view["title"], view["connected"], view["count"], view["hash"]), ("Connected", True, 3, "abc12345"))
        self.assertEqual(view["note"], "3 games on the card")
        self.call("POST", "/openmenu/launch", json.dumps({"product": "MK51035"}).encode(), {"X-Requested-With": "netswitch"})
        view = json.loads(self.call("GET", "/openmenu/view")[1])
        self.assertTrue(view["busy"])
        self.assertIn("Crazy Taxi", view["note"])
        om._state["seen"] -= om.SEEN_WINDOW + 1
        self.assertEqual(json.loads(self.call("GET", "/openmenu/view")[1])["title"], "Not connected")


if __name__ == "__main__":
    unittest.main()
