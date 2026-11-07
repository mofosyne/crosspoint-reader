"""On-device smoke test for the feed reader. Needs the Sticky on Wi-Fi with saved credentials and the
debug firmware. It rewrites /feeds/sources.txt on the SD card.

  python3 -m unittest -v feeds_smoke_test
"""
import re
import time
import unittest

from sticky_console import Sticky

SOURCES = [
    "feed https://news.ycombinator.com/rss limit=2 content=page comments=link name=HN",
    "site https://jvns.ca/ links=/blog/2026/ limit=1 name=Julia",
    "sitemap https://jvns.ca/sitemap.xml limit=1 name=Map",
]


class FeedsSmoke(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dev = Sticky()
        cls.dev.wait_ready()
        cls.cmd("FEEDSRESET", 1.5)
        cls.cmd("SRC:CLEAR")
        for line in SOURCES:
            cls.cmd("SRC:" + line)
        cls.cmd("FEEDS", 3)

    @classmethod
    def tearDownClass(cls):
        cls.dev.close()

    @classmethod
    def cmd(cls, text, wait=0.8):
        cls.dev.ser.write(("CMD:" + text + "\n").encode())
        time.sleep(wait)

    def test_1_sources_are_listed(self):
        state = self.dev.state()
        self.assertIn("HN", state)
        self.assertIn("Julia", state)
        self.assertNotIn("! line", state)  # no problems in the sources file

    def test_2_sync_saves_items(self):
        self.assertTrue(self.dev.do("ROW:1"))  # check every source
        end = time.time() + 180
        state = ""
        while time.time() < end:
            time.sleep(4)
            try:
                state = self.dev.state(timeout=15)
            except TimeoutError:
                continue
            if state.startswith(("FeedSync done", "FeedSync error")):
                break
        self.assertTrue(state.startswith("FeedSync done"), state)
        saved = int(re.search(r"saved (\d+)", state).group(1))
        failed = int(re.search(r"failed (\d+)", state).group(1))
        self.assertGreaterEqual(saved, 3, state)
        self.assertEqual(failed, 0, state)

    def test_3_saved_articles_can_be_read(self):
        self.dev.button("BACK")  # leaving the sync screen restarts the board
        time.sleep(6)
        self.dev.wait_ready(60)
        self.dev.wait_state("Home", 30)
        self.cmd("FEEDS", 3)
        self.assertIn("unread", self.dev.state())
        self.assertTrue(self.dev.do("ROW:2"))  # the HN source
        time.sleep(3)
        items = self.dev.state()
        self.assertIn("FeedItems", items)
        self.assertRegex(items, r"\n0: \* ")
        self.assertTrue(self.dev.do("ROW:0"))
        time.sleep(4)
        article = self.dev.state()
        self.assertIn("ZimArticle", article)
        self.assertRegex(article, r"page 1/\d+")
        self.assertGreater(len(article), 400)


if __name__ == "__main__":
    unittest.main()
