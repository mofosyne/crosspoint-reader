# Goal, decisions and rules

## Goal

An on-device ZIM reader that runs on the e-reader itself (not a phone or PC browser),
alongside the existing ebook capability. Content source: stock Kiwix ZIM files on the SD
card, for example Wikipedia `mini`, `nopic` and `maxi` dumps.

## Decisions the user has made

- Fork model. This work lives in the user's fork (`origin` = mofosyne/crosspoint-reader),
  not upstream. `SCOPE.md` keeps interactive apps and heavy dependencies out of upstream.
  The `experimental` branch is the fork's trunk. `develop` stays a clean mirror of upstream.
  Anything meant for upstream branches from `develop`, never from `experimental`.
- Target newer PSRAM boards only (Sticky first; the other 8 MB PSRAM boards share the
  `esp32-s3-devkitc1-n16r8` definition in `platformio.ini`). The X3/X4 (ESP32-C3, ~380 KB, no
  PSRAM) are dropped for this feature because a stock cluster decodes to about 2 MiB.
- Keep to stock ZIMs as far as possible. No re-encoded small-cluster ZIMs unless the
  user raises it.
- zstd only is acceptable. All five probed archives use zstd (plus some uncompressed
  clusters). xz was not seen; older archives that were not probed might use it.
- The user is happy to abandon the project if it is clearly infeasible. The device spike
  says it is feasible.
- A JavaScript engine on the device was considered and set aside. CrossPoint has no
  on-device JS: `plugin.js` runs in a browser and `device.json` is declarative. A native
  C++ activity is the route taken.
- The HTTP Range work (`/download` partial content) only helps browser-side readers. It is
  on its own branch and is not needed for the on-device reader.

## Working rules (from AGENTS.md / CLAUDE.md, plus this user)

- No commit without the user's explicit approval. No push without an explicit instruction
  to push. Never open or close a PR. The user writes PR text; give only factual notes.
- Commit messages: `<type>: <summary>` (summary at most 50 chars), optional body with
  facts and verification, no assistant trailers (repo rule overrides tool defaults).
  Branch names: `<type>/<short-description>`.
- Format with `./bin/clang-format-fix -g` (needs clang-format 21+; never call raw
  `clang-format`). Do not format vendored code under `lib/zstd/`.
- Firmware changes need the `firmware-handoff` skill (four independent read-only
  reviews, bounded follow-up, explicit human acceptance). Pure host-only tests and docs
  use a lighter review. Reviewers never write files, run builds or flash.
- Hardware testing belongs to the human. Give exact commands and expected results; never
  claim a hardware result yourself.
- Heap rules: use `makeUniqueNoThrow`, no bare `new`, stack locals under 256 bytes,
  `HalMemory::allocatePsram()` never falls back to internal RAM.
- Label estimates versus measurements. Cite file paths and line numbers.

## User preferences seen in this conversation

- Likes clear numbered steps for anything they must run, and expected output.
- Delegates choices ("do whatever is best value") but wants the reasoning stated.
- Cares most about article retrieval time; this is now measured and fine.
- Wants honest corrections when an earlier claim was wrong. Say so plainly.
