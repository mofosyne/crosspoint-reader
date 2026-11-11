#!/usr/bin/env python3
"""Build flashcard word lists (JLPT and HSK) for the Discover screen.

Downloads two open datasets and writes tab-separated files in the card layout
(word, reading, meaning), easiest words first, ready to copy to /wordlists on
the SD card (or upload through the file manager on the web page):

  python3 scripts/build_wordlists.py out/            # JLPT N5-N3 and HSK 1-4
  python3 scripts/build_wordlists.py out/ --jlpt 5 4 3 2 1 --hsk 1 2 3 4 5 6

Sources (the data is downloaded to your own machine and is not part of this repository):
  JLPT  https://github.com/jamsinclair/open-anki-jlpt-decks  (MIT; word lists by Jonathan Waller, CC BY)
  HSK   https://github.com/drkameleon/complete-hsk-vocabulary (MIT; the official HSK 3.0 word lists)
"""
import argparse
import csv
import io
import json
import re
import sys
import urllib.request
from pathlib import Path

JLPT_URL = "https://raw.githubusercontent.com/jamsinclair/open-anki-jlpt-decks/main/src/n{level}.csv"
HSK_URL = "https://raw.githubusercontent.com/drkameleon/complete-hsk-vocabulary/main/wordlists/exclusive/new/{level}.json"
MAX_MEANING = 140


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read().decode("utf-8")


def clean(text):
    return re.sub(r"\s+", " ", text.replace("\t", " ")).strip()


def shorten(text):
    text = clean(text)
    if len(text) <= MAX_MEANING:
        return text
    cut = text.rfind(";", 0, MAX_MEANING)
    cut = cut if cut > MAX_MEANING // 2 else text.rfind(" ", 0, MAX_MEANING)
    return text[: cut if cut > 0 else MAX_MEANING].rstrip(" ,;") + " ..."


def jlpt(level):
    rows = csv.DictReader(io.StringIO(fetch(JLPT_URL.format(level=level))))
    for row in rows:
        word, reading, meaning = clean(row["expression"]), clean(row["reading"]), shorten(row["meaning"])
        if word and meaning:
            yield word, ("" if reading == word else reading), meaning


def hsk(level):
    words = json.loads(fetch(HSK_URL.format(level=level)))
    words.sort(key=lambda w: w.get("frequency", 1 << 30))  # the most common first
    for entry in words:
        form = entry["forms"][0]
        meanings = "; ".join(form.get("meanings", [])[:3])
        reading = form.get("transcriptions", {}).get("pinyin", "")
        if entry.get("simplified") and meanings:
            yield clean(entry["simplified"]), clean(reading), shorten(meanings)


def write(path, rows):
    seen = set()
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for word, reading, meaning in rows:
            if word in seen:
                continue
            seen.add(word)
            f.write(f"{word}\t{reading}\t{meaning}\n" if reading else f"{word}\t{meaning}\n")
    return len(seen)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--jlpt", type=int, nargs="*", default=[5, 4, 3], help="JLPT levels, 5 (easiest) to 1")
    ap.add_argument("--hsk", type=int, nargs="*", default=[1, 2, 3, 4], help="HSK levels, 1 to 9")
    args = ap.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    for level in args.jlpt:
        print(f"jlpt-n{level}.tsv", write(args.outdir / f"jlpt-n{level}.tsv", jlpt(level)), "words")
    for level in args.hsk:
        print(f"hsk-{level}.tsv", write(args.outdir / f"hsk-{level}.tsv", hsk(level)), "words")
    return 0


if __name__ == "__main__":
    sys.exit(main())
