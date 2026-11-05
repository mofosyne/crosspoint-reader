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
