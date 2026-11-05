# Findings

Everything here was checked against the user's real archives unless marked "assumed".

## ZIM file layout (verified on 5 archives)

Header (80 bytes, little-endian): magic `0x44D495A`, version major/minor (u16 each), uuid,
entryCount, clusterCount, urlPtrPos, titlePtrPos, clusterPtrPos, mimeListPos (u64 each),
mainPage, layoutPage, checksumPos.

File order in every real archive: header, MIME list (at 80), the clusters (first cluster
at offset 1024), the directory entries, then the URL pointer list, title pointer list and
cluster pointer list, then a 16-byte checksum. The tables come AFTER the clusters.

- The last cluster has no stored end. Its true end is unknown; the nearest following table
  (the URL pointer list) is only an upper bound, and the directory entries lie in between.
  The library reports `ClusterInfo::exactExtent = false` for it and the decoder must stop
  at the end of its zstd frame. In these archives the last cluster is a huge uncompressed
  index, not article data.
- URL pointer list: u64 per entry, sorted by (namespace, path) by unsigned bytes. Title
  pointer list: u32 index into the URL list per entry, sorted by (namespace, title); an
  empty stored title means "same as the path".
- Directory entry: content = mime u16, paramLen u8, namespace u8, revision u32, cluster u32,
  blob u32, then path and title NUL-terminated. Redirect: mime 0xFFFF, then redirect index.
- Cluster: first byte low 4 bits = compression (0/1 stored, 4 xz, 5 zstd), bit 0x10 =
  8-byte blob offsets. Body: offset table (blobCount + 1 entries) then blobs.
- Namespaces: versions 5.0 and 6.0 files keep articles in `A`; the 6.2 file uses `C`.
  `ZimReader::findContent` tries `C` then `A` and remembers the first hit.

## Archive survey (the user's `kiwix` folder; sizes from `ls -lh`)

| Archive | Version | Entries | Clusters | Size | Compression seen |
|---|---|---|---|---|---|
| wikipedia_en_all_mini_2024-04 | 5.0 | 9,339,419 | 15,529 | 6.9 GiB | zstd + stored |
| wikipedia_en_all_nopic_2024-06 | 6.0 | 18,430,017 | 142,495 | 53 GiB | zstd + stored |
| wikipedia_en_all_maxi_2024-01 | 6.0 | 24,908,076 | 174,546 | 102 GiB | zstd + stored |
| rationalwiki_en_all_nopic_2021-03 | 5.0 | 12,765 | 115 | 76 MiB | zstd + stored |
| devdocs_en_c_2025-01 | 6.2 | 616 | 4 | 1.3 MiB | zstd + stored |

- All use 32-bit blob offsets in the sampled clusters, and `text/html` articles.
- Only 5 of about 50 files were probed. Older ones (termux 2022, openstreetmap 2023,
  stackoverflow 2023) might still use xz.
- `mini` is a subset: it has `Albert_Einstein` but not `Paris` or `Moon`. The path and title
  orderings agree on every archive, so this is the archive, not a lookup bug.
- Even `mini` (6.9 GiB) is over the FAT32 file limit, so the card must be exFAT.

## zstd details

- A compressed cluster decodes to about 2 MiB (1.83-2.09 MB over 14 clusters), from
  130-400 KB compressed.
- Declared zstd window: 8 MiB for Wikipedia and devdocs, 128 MiB for rationalwiki 2021.
- The frames do NOT declare their content size (`ZSTD_CONTENTSIZE_UNKNOWN`, measured on the
  Sticky). So the decode must go into a caller buffer large enough for the whole cluster
  (at least about 2.1 MB; 3 MiB was used) with single-shot `ZSTD_decompressDCtx`.
  A streaming decoder would reserve the full declared window. Map "destination too small"
  to `zim::DECODE_TOO_SMALL`; the spike mapped every error to a generic failure.

## Article HTML and rendering

- `Albert_Einstein`: 26,778 B of HTML in `mini`, 590,969 B in `nopic`, 696,140 B in `maxi`.
  Spike average over 21 `mini` articles: 9.6 KB (3.5-26.8 KB). `mini` is the practical target.
- Pages are full Parsoid/Minerva documents: head with inline `<style>` blocks, external
  `<script src>`, MediaWiki class names, tables, infobox, `<sup>` references.
- Existing firmware mechanism: `DictionaryDefinitionActivity` with `htmlDefinition = true`
  builds styled pages through `buildDictionaryHtmlPages` (`src/util/DictHtmlPages.cpp`):
  `writeNormalizedXhtml` (lowercases tags, self-closes void tags, drops doctype/comments,
  escapes bare `&`/`<`) into `/.crosspoint/dicthtml.tmp`, parsed by `ChapterHtmlSlimParser`.
  That parser skips `head`, `style`, `script`, `title`, and supports tables.
- Limits tuned for the C3: `MAX_STYLED_HTML_BYTES` 16 KB (`DictionaryDefinitionActivity.cpp`),
  `MAX_STYLED_PAGES` 64, `MAX_STYLED_PAGE_ELEMENTS` 512, heap floors 64 KB / 40 KB in
  `DictHtmlPages.cpp`. Larger text takes the plain-text path (`htmlToPlainText`).
- Problem 1: `writeNormalizedXhtml` fails on HTML-only named entities such as `&nbsp;`
  (expat has no DTD). Host harness on 7 real articles: 0 of 7 parse as-is, 7 of 7 parse once
  named entities are resolved with `lookupHtmlEntity` (`lib/Epub/Epub/htmlEntities.h`). The
  harness copies the normalizer verbatim and sets an expat default handler like the firmware;
  it was not run inside the firmware. Fix: resolve named entities during normalization.
- Problem 2: `htmlToPlainText` (`src/util/HtmlToPlainText.cpp`) strips tags but leaks inline
  `<style>` CSS and the page title into the text (6 KB of CSS in a 12 KB `mini` result).
- Not scoped: following links between articles, images, formulas, table layout quality,
  long articles (nopic Einstein is 183 KB of text; needs chunking or an SD-cached path
  like normal books), and layout time on the panel.

## Hardware facts

- reTerminal Sticky: ESP32-S3 QFN56 rev 0.2, 8 MB octal PSRAM, 32 MB QSPI flash
  (esptool auto-detected 32 MB; the board definition `esp32-s3-devkitc1-n16r8` says 16 MB).
- SD card on SPI: CS 8, MISO 12, SCK 13, MOSI 14. The e-paper display uses SCK 13, SDI 14,
  CS 15, so the two share SCK and MOSI (the SDK profile and the user's reading agree).
  Seeed lists no SD power-enable pin; the SDK profile has GPIO 10.
- Console and upload go through a WCH USB-UART bridge (1a86:55d3, `/dev/ttyACM0` on the
  user's PC), not native USB. Upload via `esptool` with auto-reset worked first time.
- SD backend per board (`BoardConfig.h`): Sticky and eego_a4 use SdFat over SPI; X4 Pro,
  X4 Classic, Paper Mono, Metalio and Picco default to native SDMMC (faster; untested here).
- Flash headroom: `sticky` is 80.9% full (5,303,483 of 6,553,600 B app partition).
