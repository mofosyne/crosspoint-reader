# Measurements

Measured = from a real run. Estimates are labelled. Raw device log: `logs/spike-serial-2026-11-06.log`.

## Device spike (reTerminal Sticky, `sticky_zimspike`, 2026-11-06, run by the user)

Setup: `wikipedia_en_all_mini_2024-04.zim` (7,400,302,566 B) on an exFAT card, SdFat over SPI
shared with the display, ESP32-S3 at 240 MHz, zstd 1.5.7 built `-O2` with its context in PSRAM.
Cold reads, display idle. One run, one archive.

| What | Result |
|---|---|
| Sequential SD read, 256 KB single call | 92 ms, 2.78 MB/s (three offsets, within 1%) |
| Sequential SD read, 8 x 32 KB | 90 ms, 2.83-2.85 MB/s |
| Random 512 B read (300 reads) | avg 625 us, min 550, max 703 |
| `ZimReader::open` | 4.3 ms, 2 reads |
| `findContent` (21 articles) | avg 25.2 ms, min 16.7, max 48.5; avg 46 reads (32-92) |
| `readBlob` total | avg 329.6 ms (315-356) |
| ...of which SD read | avg 55.3 ms (51-62) |
| ...of which zstd decode | avg 274.1 ms (263-294) for about 2 MiB, about 7.6 MB/s |
| Article HTML size | avg 9,568 B (3,551-26,778) |
| Title search "Albert Ein" + 5 entries | 53.6 ms, 84 reads |
| Opening an article before layout | about 355 ms (lookup + read + decode) |

The first lookup (Einstein, 92 reads) is slower because `findContent` searched namespace `C`
before `A`; the memo then halves it (about 46 reads).

Memory (internal / PSRAM): boot internal free 239,368 B, largest block 172,020 B;
PSRAM 8,383,576 B free. After two PSRAM buffers (1 MiB packed + 3 MiB plain) PSRAM free
4,090,960 B. After `ZimReader::open` plus decoder context, PSRAM free 3,994,700 B (the
zstd context is about 96 KB). At the end both are back to the starting values: no leak.
Internal min-free 231 KB.

Flash cost of the spike (`sticky`): baseline 5,303,483 B (80.9%), spike 5,377,379 B
(82.1%), so +73,896 B for zstd decoder + `ZimReader` + benchmark. Static RAM unchanged
(77,980 B).

Proposed kill thresholds (all passed): lookup under 1 s, article open under 2 s, 2 MiB
decode under 1 s, random read under 10 ms, free PSRAM over 3 MiB.

## Host measurements (read counts, `ZimRealFile.ReadCostPerOperation`)

Counted `readAt` calls and distinct 512-byte sectors per operation on the real archives.

| Operation | Reads | Sectors | Note |
|---|---|---|---|
| path lookup, first (both namespaces) | 92-100 | 75-82 | namespace `C` searched in vain first |
| path lookup, after namespace memo | 42-50 | 38-44 | |
| title-prefix search | 69-72 | 57-60 | |
| list 5 matching titles | 15 | 4-8 | |
| cluster read | 3 | 343 (mini), 508-517 (nopic/maxi) | 175-264 KB |

Reading each directory entry bounded by the next pointer (instead of a fixed 1 KiB) cut the
bytes per lookup from about 50 KB to about 3 KB and the sectors from 139-149 to 70-82.

## Estimates that were NOT confirmed

- `nopic` and `maxi` on the device: expected slightly slower (one or two more search steps,
  clusters up to 264 KB) but not measured.
- exFAT fragmentation: random reads stayed under 0.71 ms, so the user's copy looks
  contiguous. A fragmented 100 GB file could make backward seeks walk the FAT (not tested).
- Contention with a screen refresh (shared SPI bus, 48 KB frame at 20 MHz is about 20 ms
  estimated) was not measured.
- Layout time for an article through the dictionary viewer path: not measured.

## Firmware browse screen benchmark (Sticky, commit 2d6a448d, 2026-11-06)

Log: `logs/zim-browse-benchmark-2026-11-06.log`. 12 random articles, mini ZIM, real firmware path.

| Measure | Firmware | Spike |
|---|---|---|
| Read article (find + SD + decode) | 330.6 ms avg (317-340) | about 330 ms |
| Title lookup (random page) | 75.2 ms avg (74-76) | 25 ms lookup, 54 ms search |
| Whole 12-article loop | 5059 ms | n/a |
| Panel draw (clearScreen to displayBuffer) | 22-27 ms | n/a |

- Heap stayed flat (about 204 KB free, 155 KB max block); PSRAM free 8,238,772 B before and
  after, so no leak. About 90 KB less than the 8.33 MB seen earlier; stable, cause unconfirmed.
- Text-conversion time was lost to log truncation (earlier browse run: 1-2 ms).
- The draw time excludes the e-paper refresh itself.

### Does the reader need PSRAM?

Not answered by this run; it only ran with PSRAM. What the code needs: one decoded cluster
buffer of at least about 2.1 MB (clusters decode to about 2 MiB) plus the zstd context. The
internal heap here has only about 155 KB as its largest block, so a whole-cluster decode
cannot fit without PSRAM. Going without it would need streaming decode of a cluster and
stopping at the wanted blob, which also needs the zstd window (up to 8 MiB for stock ZIMs)
unless windows are small. Untested; treat PSRAM as required until measured.

## Scan of 46 stock ZIMs (2026-11-06)

Data: `logs/zim-scan-2026-11-06.csv` and `.md` (tool: `tools/zim_scan.py`). Up to 2000 clusters
sampled per file for compression and zstd window; up to 12 per file decoded for real size.

- **Compression:** only stored and zstd. No xz cluster in any of the 46 files (versions 5.0 to 6.2).
- **zstd window:** 8 MiB in 44 files; 128 MiB in the two 2021 `rationalwiki` files. Single-shot
  decode into a large enough buffer does not allocate the window separately, so this does not
  add RAM. The 128 MiB frames are within the zstd default decoder limit (2^27).
- **Decoded cluster size:** about 2 MiB (2000-2048 KiB) in nearly every file, including all
  Wikipedia variants. Larger outliers: `devdocs_en_matplotlib` 6.0 MiB, `docs.python.org` 4.8 MiB,
  `gutenberg_mul_all` 16.8 MiB (compressed 2.2 MiB). The sample is small, so the true maximum
  per file may be higher.
- **Compressed cluster size:** mostly 300-660 KiB; matplotlib up to 4.3 MiB.
- **Offsets:** see the `offsets64` column; read it before assuming 32-bit blob offsets.

### What this says about PSRAM

A cluster is one zstd frame that back-references across its whole 2 MiB, so reading any
article needs the whole decoded cluster (or equivalent window) in RAM. Streaming does not
avoid that, because the window (8 MiB) is larger than the cluster. The ESP32-C3 has about
380 KB usable RAM and the Sticky's internal heap had a 155 KB largest block. So stock ZIMs
need PSRAM of at least about 2.1 MB for Wikipedia and a larger buffer (or a "too large"
refusal) for outlier files such as Gutenberg and matplotlib. Not tested: re-encoding ZIMs
with small clusters, which would break the "stock ZIM" goal.

### Estimated per-article time on the Sticky (model, not measured)

Tool: `tools/zim_estimate.py`; table: `logs/zim-estimate-2026-11-06.md`. Model fitted to the
mini-Wikipedia run: 0.54 ms per directory read (reads grow with log2 of entry count), 0.36 ms
per KiB of compressed cluster from SD, 137 ms per MiB of decoded output.

- Wikipedia (mini, nopic, maxi), StackExchange, schools, wikiversity: about 340-380 ms typical,
  worst 370-470 ms. Dominated by zstd decode of a 2 MiB cluster (about 270 ms).
- `docs.python.org` worst about 1.0 s (4.8 MiB cluster).
- `devdocs_matplotlib` worst about 2.4 s and `gutenberg_mul_all` worst about 3.2 s; both
  exceed the 8 MB PSRAM budget (6.0 and 16.8 MiB decoded), so they would need a refusal.
- The model gives 353 ms for mini against about 405 ms measured (330 ms read plus 75 ms title
  lookup), so treat the numbers as roughly 15% low. Decode scales with decoded size only;
  real speed also varies with how compressible the text is. Stored (uncompressed) clusters
  are not covered because the scan did not record their size.
