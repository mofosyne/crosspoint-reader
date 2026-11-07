"""On-device smoke test for the ZIM reader. Run with the Sticky plugged in (debug firmware):

  STICKY_ZIM=/zim/wikipedia_en_for_schools_maxi_2024-08.zim python3 -m unittest -v zim_smoke_test

Each test starts from the board's Home screen (opening the port resets it).
"""
import os
import re
import time
import unittest

from sticky_console import Sticky

ZIM = os.environ.get("STICKY_ZIM", "/zim/wikipedia_en_for_schools_maxi_2024-08.zim")


def page_of(state):
    m = re.search(r"page (\d+)/(\d+)", state)
    return int(m.group(1)), int(m.group(2))


class ZimSmoke(unittest.TestCase):
    def setUp(self):
        self.dev = Sticky()
        self.dev.wait_ready()  # opening the port reset the board
        self.dev.wait_state("Home", 20)

    def tearDown(self):
        self.dev.close()

    def open_archive(self):
        self.assertTrue(self.dev.zim(ZIM))
        return self.dev.wait_state("ZimBrowse", 40, poll=2)

    def test_lowercase_search_finds_capitalised_title(self):
        self.open_archive()
        self.assertTrue(self.dev.do("SEARCH:moon"))
        rows = self.dev.state()
        self.assertRegex(rows, r"\n2: Moon\n")

    def test_article_has_pages_and_headings(self):
        self.open_archive()
        self.dev.do("SEARCH:Moon")
        self.assertTrue(self.dev.do("ROW:2"))
        state = self.dev.wait_state("ZimArticle 'Moon'", 30)
        self.assertGreater(page_of(state)[1], 20)
        self.assertTrue(self.dev.do("HEADING:2"))
        self.assertGreater(page_of(self.dev.state())[0], 1)

    def test_follow_a_link_and_go_back(self):
        self.open_archive()
        self.assertTrue(self.dev.do("MAIN"))
        state = self.dev.wait_state("ZimArticle", 30)
        total = page_of(state)[1]
        for page in range(1, total + 1):
            self.dev.do("PAGE:%d" % page)
            state = self.dev.state()
            if re.search(r"\n0: .+ -> .+", state):
                break
        else:
            self.fail("no page with a link")
        origin = page_of(state)[0]
        self.assertTrue(self.dev.do("LINK:0"))
        state = self.dev.wait_state("depth 1", 30)
        self.assertIn("ZimArticle", state)
        self.assertTrue(self.dev.do("BACK"))
        state = self.dev.wait_state("depth 0", 10)
        self.assertEqual(page_of(state)[0], origin)

    def test_titles_have_no_escaped_markup(self):
        self.open_archive()
        for _ in range(3):
            rows = self.dev.state()
            self.assertNotIn("&lt;", rows)
            self.dev.do("NEXT")


if __name__ == "__main__":
    unittest.main()
