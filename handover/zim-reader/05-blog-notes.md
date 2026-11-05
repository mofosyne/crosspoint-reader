# Notes for a possible blog post (factual pointers, not prose)

The user may write a blog post later. These are the story beats and where the evidence is.

- Why a fork and not a plugin: CrossPoint has no on-device scripting; `plugin.js` runs in a
  browser. See `02-findings.md` (Goal section in `01-goal-and-rules.md`).
- Surveying real ZIMs before designing: `logs/zim-probe-output-2026-11-05.txt` and
  `tools/zim_probe.py`. Surprises: all zstd, ~2 MiB clusters, 8 MiB or 128 MiB windows,
  frames without a declared size.
- A wrong assumption caught by review plus real data: the last cluster does not end at the
  checksum (`logs/zim-layout-and-read-cost.txt`).
- Counting reads before touching hardware, then cutting sectors per lookup in half
  (`logs/zim-layout-and-read-cost.txt`).
- The renderer question answered with host tests: `&nbsp;` breaks the existing normalizer
  (`logs/renderer-tests-2026-11-05.txt`).
- The hardware spike on a reTerminal Sticky: `logs/spike-serial-2026-11-06.log`,
  summary in `03-measurements.md`. Headline: about 0.35 s to open a Wikipedia article from a
  6.9 GB archive on an ESP32-S3, 83% of it zstd decoding.
- Other details: build sizes in `logs/build-sizes.txt`; shared SPI bus facts in `02-findings.md`.
