#!/usr/bin/env python3
"""Shrink a CJK font so it fits the Sticky's PSRAM.

A full Noto Sans CJK font (65,000 glyphs, 16 MB) makes the font engine keep several megabytes of character
tables in PSRAM for each text size, which leaves too little for feed sync and the ZIM reader. This keeps
what flashcards and reading need: ASCII and accented Latin (pinyin), kana, punctuation, every kanji of JIS X 0208,
every hanzi of GB 2312, and any characters used by word lists you pass in. The result is about 2.6 MB and 12,000 glyphs.

  python3 scripts/subset_cjk_font.py NotoSansCJKjp-Regular.otf NotoSansCJKjp-Regular.otf.small out/*.tsv
  # then copy it to /fonts/NotoSansCJKjp/NotoSansCJKjp-Regular.otf on the SD card

Needs fontTools (pip install fonttools). Noto Sans CJK is available under the SIL Open Font License from
https://github.com/notofonts/noto-cjk (Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf). The Sticky build enables the
CFF font driver (FREEINK_FONT_ENABLE_CFF) so this OpenType/CFF file loads.
"""
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

RANGES = [(0x20, 0x7E), (0xA0, 0x24F), (0x2000, 0x206F), (0x2190, 0x21FF), (0x25A0, 0x25FF),
          (0x3000, 0x30FF), (0x31F0, 0x31FF), (0xFF00, 0xFFEF)]


def wanted(extra_files):
    chars = set()
    for lo, hi in RANGES:
        chars.update(range(lo, hi + 1))
    for encoding, first_lead, last_lead in (("gb2312", 0xA1, 0xF7), ("euc_jp", 0xA1, 0xF4)):
        for lead in range(first_lead, last_lead + 1):
            for trail in range(0xA1, 0xFF):
                try:
                    chars.add(ord(bytes([lead, trail]).decode(encoding)))
                except UnicodeDecodeError:
                    pass
    for path in extra_files:
        with open(path, encoding="utf-8") as f:
            chars.update(ord(c) for c in f.read())
    chars -= {0x09, 0x0A, 0x0D}
    return chars


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    source, target, extra = sys.argv[1], sys.argv[2], sys.argv[3:]
    options = subset.Options()
    options.layout_features = []  # no kerning or alternates: smaller, and the readers do not use them
    options.hinting = False
    font = TTFont(source)
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=sorted(wanted(extra)))
    subsetter.subset(font)
    font.save(target)
    print(f"{target}: {len(font.getBestCmap())} characters, {font['maxp'].numGlyphs} glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
