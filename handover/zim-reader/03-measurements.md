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
