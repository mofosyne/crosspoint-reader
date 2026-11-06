# Vendored zstd (decoder only)

- Upstream: https://github.com/facebook/zstd, release v1.5.7
- Release tarball `zstd-1.5.7.tar.gz`, sha256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`
- License: BSD-3-Clause (`LICENSE`, the BSD option of upstream's dual license)
- Source files are byte-identical to upstream `lib/` at that release. Nothing is patched.

## What is included

`lib/common`, `lib/decompress` (without the `.S` assembly file), `zstd.h`, `zstd_errors.h`.
Left out: the compressor, dictionary builder, legacy formats, threading and pool sources
(`pool.c`, `threading.c`), and the x86 Huffman assembly.

## Build flags (`library.json`)

`ZSTD_DISABLE_ASM`, `ZSTD_LEGACY_SUPPORT=0`, `ZSTD_STRIP_ERROR_STRINGS`, `ZSTD_NO_TRACE`,
`ZSTD_MULTITHREAD=0`. Only the decoder API is used (`ZimReader` clusters).

## Updating

1. Download the new tarball and record its sha256 here.
2. Replace the files from upstream without editing them, then check with
   `diff -r` against the tarball's `lib/` for the listed directories.
3. Re-run the host tests (`ZimZstd*`) and compare the `sticky` flash size.

Do not run clang-format on this folder (`bin/clang-format-fix` skips it).
