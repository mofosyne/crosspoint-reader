"""Read-only scan of every .zim in a folder; writes a CSV and a Markdown report.

Usage: python3 -I zim_scan.py /path/to/zim_folder out_prefix
Writes out_prefix.csv and out_prefix.md. Needs the `zstandard` package.

Per file: header, compression kinds and zstd window sizes over up to 2000 sampled clusters,
and the real decoded size of up to 12 sampled clusters (largest compressed, quantiles).
"""
import collections
import csv
import glob
import os
import statistics
import struct
import sys

import zstandard

COMP = {1: "none", 4: "xz", 5: "zstd"}
FIELDS = [
    "file", "gib", "version", "entries", "clusters", "sampled", "compression",
    "offsets64", "zstd_window_max_mib", "zstd_window_min_mib", "comp_median_kib",
    "comp_max_kib", "decoded_max_kib", "decoded_median_kib", "decoded_n", "error",
]


def scan(path):
    row = dict.fromkeys(FIELDS, "")
    row["file"] = os.path.basename(path)
    size = os.path.getsize(path)
    row["gib"] = "%.2f" % (size / 2**30)
    with open(path, "rb") as f:
        h = f.read(80)
        magic, major, minor = struct.unpack("<IHH", h[:8])
        if magic != 0x44D495A:
            row["error"] = "bad magic"
            return row
        entries, clusters = struct.unpack("<II", h[24:32])
        cluster_ptr = struct.unpack("<Q", h[48:56])[0]
        (checksum_pos,) = struct.unpack("<Q", h[72:80])
        row.update(version="%d.%d" % (major, minor), entries=entries, clusters=clusters)
        f.seek(cluster_ptr)
        offs = list(struct.unpack("<%dQ" % clusters, f.read(clusters * 8)))
        offs.append(checksum_pos)
        step = max(1, clusters // 2000)
        sample = list(range(0, clusters, step))
        kinds = collections.Counter()
        wins = []
        sizes = []
        off64 = 0
        for i in sample:
            f.seek(offs[i])
            head = f.read(1 + 18)
            comp = head[0] & 0x0F
            kinds[COMP.get(comp, "unknown(%d)" % comp)] += 1
            off64 += bool(head[0] & 0x10)
            if comp == 5:
                try:
                    wins.append(zstandard.get_frame_parameters(head[1:]).window_size)
                except Exception:
                    pass
            if comp in (4, 5):
                sizes.append((offs[i + 1] - offs[i], i, comp))
        row["sampled"] = len(sample)
        row["compression"] = "/".join("%s:%d" % kv for kv in sorted(kinds.items()))
        row["offsets64"] = "%d/%d" % (off64, len(sample))
        if wins:
            row["zstd_window_max_mib"] = "%.2f" % (max(wins) / 2**20)
            row["zstd_window_min_mib"] = "%.2f" % (min(wins) / 2**20)
        if sizes:
            sizes.sort()
            row["comp_median_kib"] = "%.0f" % (statistics.median(s for s, _, _ in sizes) / 1024)
            row["comp_max_kib"] = "%.0f" % (sizes[-1][0] / 1024)
            n = len(sizes)
            picks = {sizes[min(n - 1, int(n * q))] for q in (0, .1, .25, .5, .75, .9, .99, 1)}
            picks.add(sizes[-1])
            out_sizes = []
            for csize, i, comp in sorted(picks)[-12:]:
                f.seek(offs[i] + 1)
                body = f.read(csize - 1)
                try:
                    if comp == 5:
                        out = zstandard.ZstdDecompressor().decompressobj().decompress(body)
                    else:
                        import lzma
                        out = lzma.decompress(body)
                    out_sizes.append(len(out))
                except Exception as e:
                    row["error"] = "decode %r" % (e,)
            if out_sizes:
                row["decoded_max_kib"] = "%.0f" % (max(out_sizes) / 1024)
                row["decoded_median_kib"] = "%.0f" % (statistics.median(out_sizes) / 1024)
                row["decoded_n"] = len(out_sizes)
    return row


def main(folder, prefix):
    rows = []
    for p in sorted(glob.glob(os.path.join(folder, "*.zim"))):
        print(os.path.basename(p), flush=True)
        try:
            rows.append(scan(p))
        except Exception as e:
            r = dict.fromkeys(FIELDS, "")
            r["file"], r["error"] = os.path.basename(p), repr(e)
            rows.append(r)
    with open(prefix + ".csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, FIELDS)
        w.writeheader()
        w.writerows(rows)
    with open(prefix + ".md", "w") as fh:
        fh.write("| " + " | ".join(FIELDS) + " |\n|" + "---|" * len(FIELDS) + "\n")
        for r in rows:
            fh.write("| " + " | ".join(str(r[k]) for k in FIELDS) + " |\n")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(*sys.argv[1:]))
