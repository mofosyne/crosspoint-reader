"""Read-only ZIM probe: header, mime types, cluster compression and sizes.

Usage: python3 -I zim_probe.py /path/to/file.zim
"""
import collections
import os
import statistics
import struct
import sys

COMP = {1: "none", 4: "xz/lzma2", 5: "zstd"}


def main(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        h = f.read(80)
        magic, major, minor = struct.unpack("<IHH", h[:8])
        if magic != 0x44D495A:
            print("not a ZIM file (magic %#x)" % magic)
            return 1
        entries, clusters = struct.unpack("<II", h[24:32])
        url_ptr, title_ptr, cluster_ptr, mime_ptr = struct.unpack("<QQQQ", h[32:64])
        main_page, layout_page = struct.unpack("<II", h[64:72])
        (checksum_pos,) = struct.unpack("<Q", h[72:80])
        print("file size       : %d bytes (%.2f GiB)" % (size, size / 2**30))
        print("version         : %d.%d" % (major, minor))
        print("entries         : %d" % entries)
        print("clusters        : %d" % clusters)
        print("checksum pos    : %d" % checksum_pos)

        f.seek(mime_ptr)
        raw = f.read(8192).split(b"\0")
        mimes = []
        for m in raw:
            if not m:
                break
            mimes.append(m.decode("ascii", "replace"))
        print("mime types (%d) : %s" % (len(mimes), ", ".join(mimes[:12])))

        f.seek(cluster_ptr)
        table = f.read(clusters * 8)
        offs = list(struct.unpack("<%dQ" % clusters, table))
        offs.append(checksum_pos)

        step = max(1, clusters // 2000)
        sample = list(range(0, clusters, step))
        kinds = collections.Counter()
        ext = collections.Counter()
        sizes = []
        for i in sample:
            f.seek(offs[i])
            b = f.read(1)[0]
            kinds[COMP.get(b & 0x0F, "unknown(%d)" % (b & 0x0F))] += 1
            ext["64-bit offsets" if b & 0x10 else "32-bit offsets"] += 1
            sizes.append((offs[i + 1] - offs[i], i, b & 0x0F))
        print("sampled clusters: %d (every %d)" % (len(sample), step))
        print("compression     : %s" % dict(kinds))
        print("blob offsets    : %s" % dict(ext))
        packed = sorted(s for s, _, c in sizes if c in (4, 5))
        raw = sorted(s for s, _, c in sizes if c == 1)
        print(
            "compressed clusters: min %d / median %d / max %d bytes"
            % (packed[0], statistics.median(packed), packed[-1])
        )
        if raw:
            print("uncompressed clusters: %d, largest %d bytes" % (len(raw), raw[-1]))

        # Decompress a few compressed clusters (largest, 90th percentile, median)
        # to learn the real RAM needs.
        pick = {packed[-1], packed[int(len(packed) * 0.9)], packed[len(packed) // 2]}
        chosen = [(s, i) for s, i, c in sizes if c in (4, 5) and s in pick][:5]
        for csize, i in sorted(chosen, reverse=True):
            f.seek(offs[i])
            data = f.read(csize)
            comp = data[0] & 0x0F
            body = data[1:]
            try:
                if comp == 4:
                    import lzma

                    out = lzma.decompress(body)
                    print("cluster %d: xz %d -> %d bytes" % (i, csize, len(out)))
                elif comp == 5:
                    import zstandard

                    params = zstandard.get_frame_parameters(body)
                    out = zstandard.ZstdDecompressor().decompressobj().decompress(body)
                    declared = (
                        "unknown"
                        if params.content_size in (-1, 2**64 - 1, 2**64 - 2)
                        else str(params.content_size)
                    )
                    print(
                        "cluster %d: zstd %d -> %d bytes, window %d bytes, "
                        "declared content size %s"
                        % (i, csize, len(out), params.window_size, declared)
                    )
                else:
                    print("cluster %d: compression %d, skipped" % (i, comp))
            except ImportError as e:
                print("cluster %d: cannot decompress (%s)" % (i, e))
            except Exception as e:  # report and continue
                print("cluster %d: decompress failed: %r" % (i, e))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
