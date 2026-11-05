import struct, sys, os
for path in sys.argv[1:]:
    with open(path, "rb") as f:
        h = f.read(80)
        _, major, minor = struct.unpack("<IHH", h[:8])
        entries, clusters = struct.unpack("<II", h[24:32])
        url, title, cptr, mime = struct.unpack("<QQQQ", h[32:64])
        (chk,) = struct.unpack("<Q", h[72:80])
        f.seek(cptr)
        first = struct.unpack("<Q", f.read(8))[0]
        f.seek(cptr + 8 * (clusters - 1))
        last = struct.unpack("<Q", f.read(8))[0]
        f.seek(cptr + 8 * (clusters - 2))
        prev = struct.unpack("<Q", f.read(8))[0]
    print(os.path.basename(path), "v%d.%d" % (major, minor))
    print("  mime %d  urlPtr %d  titlePtr %d  clusterPtr %d" % (mime, url, title, cptr))
    print("  firstCluster %d  prevCluster %d  lastCluster %d  checksum %d" % (first, prev, last, chk))
    print("  last cluster extent to checksum: %d bytes; tables after last cluster: %s" % (
        chk - last, any(p > last for p in (url, title, cptr, mime))))
