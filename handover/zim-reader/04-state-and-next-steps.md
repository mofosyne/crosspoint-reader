# State and next steps

## Branches (see `git branch -vv` for the truth)

| Branch | Content | Pushed? |
|---|---|---|
| `develop` | upstream integration base, 5936f485 | mirror of `origin/develop` |
| `http-range-download` | `/download` HTTP Range support, 98df1974 (browser-side readers only) | yes, before this handover |
| `experimental` | fork trunk: `develop` + the ZIM reader library (`816546ec`) + this folder (two docs commits on top) | pushed with this handover |
| `test/zim-device-spike` | `experimental` + two commits: `1f005d4e` vendored zstd 1.5.7 decoder, `3aca5f0d` throwaway benchmark (`src/spike/`, `sticky_zimspike` env). Built and flashed before the final formatter pass; the formatting-only change was not rebuilt. | pushed with this handover |
| `feat/js-file-api` | empty; safe to delete | no |

The Range branch is NOT merged into `experimental`. Merge it only if the user wants it.

## What exists

- `lib/ZimReader/ZimReader.{h,cpp}`: read-only ZIM reader (5.x and 6.x) over the existing
  `freeink::content::ByteSource`. Header and MIME list, URL-order binary search,
  title-order search, redirects, stored and compressed clusters, 4- and 8-byte blob
  offsets. Decompression is a caller-supplied `ClusterDecoder`; the caller owns the packed
  and plain buffers. Library state: a 1 KiB entry buffer plus the MIME list.
- `test/zim_reader/`: 23 synthetic-ZIM tests (laid out like real archives) plus 5
  real-file tests that skip unless `ZIM_TEST_FILE` is set. Builds with
  `-Wall -Wextra -pedantic`, also clean under ASan/UBSan. libzstd is optional for the tests.
- Spike branch only: `lib/zstd/` (decoder-only zstd 1.5.7, sha256 of the release tarball
  `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`), `src/spike/ZimSpike.*`,
  a `#ifdef ZIM_SPIKE` hook after `HalSystem::checkPanic()` in `src/main.cpp`, and the
  `sticky_zimspike` env in `platformio.ini` (it needs its own `CROSSPOINT_VERSION` because
  `scripts/git_branch.py` only injects it for `default` and `sticky`).

## Build and test commands

```sh
git submodule update --init freeink-sdk          # pinned 5e94fe74
# host tests
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test -j && ctest --test-dir build/test -j      # 908 tests at last full run
ctest --test-dir build/test -R "ZimReader|ZimRealFile"
# real archive (the user's machine only)
ZIM_TEST_FILE=/path/to/file.zim build/test/zim_reader/ZimReaderTest --gtest_filter='ZimRealFile.*'
# firmware (first time: see docs/contributing/getting-started.md, PlatformIO core v6.1.19)
python3 -m venv .venv && . .venv/bin/activate
python -m pip install --upgrade 'https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip'
python -m pip install uv && pio pkg install -e sticky
uv pip install --python ~/.platformio/penv/bin/python 'pioarduino==6.1.19'
pio run -e sticky                                  # about 1-6 min
./bin/clang-format-fix -g
```
`pio check` was not run because `cppcheck` was missing locally. The cloud session cannot
flash the Sticky or read the user's ZIM files; ask the user to run those steps.

Spike run (user): `pio run -e sticky_zimspike -t upload --upload-port /dev/ttyACM0`, then
`pio device monitor -p /dev/ttyACM0 -b 115200`, lines tagged `ZSPK`. Restore with
`pio run -e sticky -t upload --upload-port /dev/ttyACM0`. A file `/zim/skip_spike` on the
card makes the benchmark skip itself.

## Ordered next steps

1. Port to `experimental` as a feature-gated build for PSRAM boards (gate on
   `HalMemory::getPsramHeap().totalBytes`, not on the board name alone):
   a. Vendor or depend on the zstd decoder; keep it out of C3 builds so their flash is unchanged.
   b. A `ByteSource` over `HalFile` (copy the shape of `SdByteSource` in `lib/Epub/ContentProtection.cpp`).
   c. A `ClusterDecoder` that maps zstd "destination too small" to `DECODE_TOO_SMALL`,
      allocates its context in PSRAM, and trims trailing bytes with `ZSTD_findFrameCompressedSize`.
   d. PSRAM workspace via `HalMemory::allocatePsram` (plain buffer at least about 2.1 MB;
      3 MiB worked).
2. Minimal ZIM screen: list `*.zim` under `/zim`, title-prefix search (check what text input
   exists, for example the Wi-Fi password keyboard), results list, open an article.
3. Article viewer on the dictionary viewer path: resolve named HTML entities in the
   normalizer (`lookupHtmlEntity`), skip style/script/head in `htmlToPlainText`, raise the
   styled caps on PSRAM boards, and decide how long articles page (chunking or an
   SD-cached one-chapter book).
4. Measure on the device: layout time, refresh plus SD contention, `nopic`/`maxi`.
5. Run the `firmware-handoff` process before calling any firmware change ready.

## Known risks and open questions

- Cluster decoded size is unknown up front. Retry with a larger plain buffer on
  `BufferTooSmall`; decide the maximum.
- Stack: the spike ran on the Arduino loop task; check the real activity stacks.
- Shared SPI bus between SD and display on the Sticky: unmeasured under load.
- Flash: `sticky` is at 82.1% with the spike; the real port should be smaller.
- Only the Sticky is tested. Other PSRAM boards and SDMMC boards are untested.
- Not yet decided: input method for search, settings, how to handle links and images,
  whether the zstd decoder lives in `lib/` or the SDK, and whether to send the Range work
  upstream (the user writes any PR text).
- The reviewers' deferred items for `ZimReader`: chunked cluster reads (wait for timing),
  caching a decoded cluster, `Entry` string copies, reading the info byte together with the
  cluster, and extra reads in the binary search.
