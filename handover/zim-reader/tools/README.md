# Throwaway probe tools

Run Python with `python3 -I script.py ...` (isolated mode) and keep archives in their own folder.
- `zim_probe.py <file.zim>`: header, MIME types, cluster compression, sizes; decodes a few
  clusters (needs the `zstandard` Python package) and prints window size and declared size.
- `zim_layout.py <files...>`: positions of the tables versus the last cluster.
- `render_probe.cpp.txt` (copy to `render_probe.cpp` to build): reads an article with `ZimReader` + libzstd, runs `htmlToPlainText`.
  Build: `g++ -std=c++20 -Itest/zim_reader/stubs -Ilib/ZimReader -Ilib/Memory -Ifreeink-sdk/libs/book/ContentProtection/include -Isrc/util -Ilib/Epub render_probe.cpp lib/ZimReader/ZimReader.cpp src/util/HtmlToPlainText.cpp lib/Epub/Epub/htmlEntities.cpp -lzstd`
- `xhtml_probe2.cpp.txt` (copy to `xhtml_probe2.cpp` to build): runs the repo's `writeNormalizedXhtml` (copied verbatim) plus expat on
  article HTML, with and without named entities resolved.
  Build: `g++ -std=c++20 -Ilib/Epub xhtml_probe2.cpp lib/Epub/Epub/htmlEntities.cpp -lexpat`
- `zim_scan.py <folder> <out_prefix>`: scans every `*.zim` in a folder, writes `<out_prefix>.csv` and `.md`
  (compression kinds, zstd window sizes, real decoded cluster sizes of up to 12 sampled clusters).
- `zim_estimate.py <scan.csv> <out.md>`: typical and worst per-article time on the Sticky, from the scan CSV (model fitted to the benchmark).
- `sticky_console.py`: library, CLI and web GUI for the debug firmware (`CROSSPOINT_SERIAL_INPUT`): screenshot, `STATE` text, `DO:` commands, taps and buttons. `serve` opens a page with the live screen (click to tap), buttons and a command box.
- `zim_smoke_test.py`: on-device unittest for the ZIM reader built on `sticky_console.py` (`python3 -m unittest -v zim_smoke_test`).
- `feeds_smoke_test.py`: on-device unittest for the feed reader (sync, list, read); needs Wi-Fi credentials saved on the Sticky.
