#!/usr/bin/env python3
"""Battery report from the Sticky's climate log.

The firmware logs the room's temperature and humidity and the battery (percent and millivolts), with what woke the
device and whether it was charging, to /feeds/climate2.bin on the SD card. Copy that file to your PC (or download
http://<device>/download?path=/feeds/climate2.bin while File Transfer runs) and:

  python3 scripts/battery_report.py climate2.bin            # summary
  python3 scripts/battery_report.py climate2.bin --hours    # plus one line per hour

The estimate counts only stretches where the battery was neither charging nor on external power. The gauge reports
whole percents, so trust it more the longer the log runs; a day or two of unplugged use gives a usable number.
Record layout (12 bytes, little endian): when u32, temperature i16 (tenths of a degree), humidity u8,
battery percent u8 (0 = not recorded), millivolts u16, flags u8 (bit 0 charging, bit 1 external power, top four
bits the wake kind), awake u8 (10 ms steps, sensor wakes only).
"""
import argparse
import datetime
import struct
import sys
from collections import Counter

KINDS = {0: "other", 1: "button", 2: "sync", 3: "redraw", 4: "page", 5: "sensor", 6: "dashboard"}
RECORD = struct.Struct("<IhBBHBB")
MAX_GAP_HOURS = 12  # across a longer gap the device may have been charged or used without leaving a reading


def load(path):
    data = open(path, "rb").read()
    out = []
    for at in range(0, len(data) - RECORD.size + 1, RECORD.size):
        when, temp, hum, battery, mv, flags, awake = RECORD.unpack_from(data, at)
        out.append(dict(when=when, temp=temp / 10, hum=hum, battery=battery, mv=mv, charging=bool(flags & 1),
                        external=bool(flags & 2), kind=KINDS.get(flags >> 4, str(flags >> 4)), awake_ms=awake * 10))
    out.sort(key=lambda r: r["when"])
    return out


def stamp(t):
    return datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime("%a %d %H:%M")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--hours", action="store_true", help="also print one line per hour")
    args = ap.parse_args()
    records = load(args.log)
    if len(records) < 2:
        print("Not enough readings yet.")
        return 0

    print(f"{len(records)} readings, {stamp(records[0]['when'])} to {stamp(records[-1]['when'])} UTC")
    print("Wakes:", ", ".join(f"{k} {n}" for k, n in sorted(Counter(r["kind"] for r in records).items())))
    sensor = [r["awake_ms"] for r in records if r["kind"] == "sensor" and r["awake_ms"] > 0]
    if sensor:
        print(f"Sensor wakes: {len(sensor)} timed, awake {sum(sensor) / len(sensor):.0f} ms on average "
              f"(from the start of the program; the boot loader adds roughly 250 ms)")

    drop = hours = mv_drop = 0.0
    segments = 0
    for a, b in zip(records, records[1:]):
        dt = (b["when"] - a["when"]) / 3600
        if not 0 < dt <= MAX_GAP_HOURS:
            continue
        if a["charging"] or a["external"] or b["charging"] or b["external"]:
            continue
        if not (a["battery"] and b["battery"]):
            continue
        if a["battery"] >= 100 and b["battery"] >= 100:
            continue  # full: almost certainly on the charger (this board cannot tell us the cable is in)
        drop += max(0, a["battery"] - b["battery"])
        mv_drop += max(0, a["mv"] - b["mv"]) if a["mv"] and b["mv"] else 0
        hours += dt
        segments += 1
    if hours < 1:
        print("Not enough unplugged time yet for a battery estimate (need an hour or more with the cable out).")
    else:
        rate = drop / hours
        print(f"Unplugged: {hours:.1f} h over {segments} stretches, battery down {drop:.0f} points ({mv_drop:.0f} mV)")
        if rate > 0:
            print(f"Drain: {rate:.2f} %/hour  ->  about {100 / rate / 24:.1f} days from full to empty")
        else:
            print("The battery has not moved in that time; the gauge reads whole percents, so log for longer.")

    if args.hours:
        print("\nhour (UTC)        readings  sensor  battery   mV    room")
        buckets = {}
        for r in records:
            buckets.setdefault(r["when"] // 3600, []).append(r)
        for hour, rs in sorted(buckets.items()):
            state = "charging" if any(r["charging"] or r["external"] for r in rs) else ""
            print(f"{stamp(hour * 3600):<17} {len(rs):>8} {sum(r['kind'] == 'sensor' for r in rs):>7} "
                  f"{rs[0]['battery']:>4}->{rs[-1]['battery']:<3} {rs[-1]['mv']:>5}  {rs[-1]['temp']:.1f}C {rs[-1]['hum']}%  {state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
