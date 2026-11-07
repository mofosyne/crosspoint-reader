"""Estimate per-article read time on the Sticky from a zim_scan CSV.

Usage: python3 -I zim_estimate.py zim-scan.csv out.md
Model (fitted to the Sticky mini-Wikipedia benchmark, see 03-measurements.md):
  find   = 0.54 ms * reads, reads scale with log2(entries) (46 reads at the mini ZIM)
  SD     = 0.36 ms per KiB of compressed cluster (2.78 MB/s sequential)
  decode = 137 ms per MiB of decoded output (274 ms for a 2 MiB cluster)
Typical uses the median sizes; worst uses the largest sampled sizes.
Stored clusters are not covered (the scan does not record their size).
"""
import csv
import math
import sys

FIND_MS_PER_READ, SD_MS_PER_KIB, DECODE_MS_PER_MIB = 0.54, 0.36, 137.0
PSRAM_KIB = 8000  # largest free PSRAM block seen was about 8.1 MB


def main(src, dst):
    rows = [r for r in csv.DictReader(open(src)) if r["decoded_max_kib"]]
    mini = next(r for r in rows if "all_mini" in r["file"])
    ref = math.log2(int(mini["entries"]))
    out = ["| file | entries | typical ms | worst ms | worst decoded MiB | fits 8 MB PSRAM |", "|---|---|---|---|---|---|"]
    for r in sorted(rows, key=lambda r: float(r["decoded_max_kib"])):
        reads = 46 * math.log2(max(2, int(r["entries"]))) / ref
        find = reads * FIND_MS_PER_READ
        t = find + float(r["comp_median_kib"]) * SD_MS_PER_KIB + float(r["decoded_median_kib"]) / 1024 * DECODE_MS_PER_MIB
        w = find + float(r["comp_max_kib"]) * SD_MS_PER_KIB + float(r["decoded_max_kib"]) / 1024 * DECODE_MS_PER_MIB
        fits = float(r["comp_max_kib"]) + float(r["decoded_max_kib"]) <= PSRAM_KIB
        out.append("| %s | %s | %.0f | %.0f | %.1f | %s |" % (
            r["file"], r["entries"], t, w, float(r["decoded_max_kib"]) / 1024, "yes" if fits else "NO"))
    open(dst, "w").write("\n".join(out) + "\n")
    print("\n".join(out))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    main(*sys.argv[1:])
