#!/usr/bin/env python3
"""Decode a SavvyCAN GVRET CSV capture into NMEA 2000 terms.

SavvyCAN shows raw CAN frames; this turns the 29-bit identifiers into PGN and
source address, and expands the PGNs this project cares about into fields.

Capture in SavvyCAN (File > Save Captured Data > "Generic CSV" or "GVRET"),
then:

    python tools/n2k_decode.py capture.csv
    python tools/n2k_decode.py capture.csv --pgn 127245
    python tools/n2k_decode.py capture.csv --src 25

Reads stdin when no file is given. Standard library only.
"""

import argparse
import csv
import math
import sys

# PGNs worth naming. Everything else prints as a bare number.
PGN_NAMES = {
    59904: "ISO Request",
    60928: "ISO Address Claim",
    126993: "Heartbeat",
    126996: "Product Information",
    126998: "Configuration Information",
    127245: "Rudder",
    127250: "Vessel Heading",
    127257: "Attitude",
}

# NMEA 2000 "not available" for a signed 16-bit field.
N2K_INT16_NA = 0x7FFF


def decode_can_id(can_id):
    """Split a 29-bit J1939/NMEA 2000 identifier.

    PF < 240 is PDU1, where PS carries the destination address and the PGN's
    low byte is zero. PF >= 240 is PDU2 (broadcast) and PS is part of the PGN.
    """
    priority = (can_id >> 26) & 0x07
    edp = (can_id >> 25) & 0x01
    dp = (can_id >> 24) & 0x01
    pf = (can_id >> 16) & 0xFF
    ps = (can_id >> 8) & 0xFF
    src = can_id & 0xFF

    if pf < 240:
        pgn = (edp << 17) | (dp << 16) | (pf << 8)
        dest = ps
    else:
        pgn = (edp << 17) | (dp << 16) | (pf << 8) | ps
        dest = 0xFF

    return priority, pgn, src, dest


def int16_le(data, offset):
    """Signed little-endian 16-bit field; None when NMEA 2000 'not available'."""
    if offset + 1 >= len(data):
        return None
    raw = data[offset] | (data[offset + 1] << 8)
    if raw == N2K_INT16_NA:
        return None
    if raw > 0x7FFF:
        raw -= 0x10000
    return raw


def rad_field_to_deg(data, offset):
    """0.0001 rad/bit field -> degrees, or None if not available."""
    raw = int16_le(data, offset)
    if raw is None:
        return None
    return math.degrees(raw * 0.0001)


RUDDER_DIRECTION = {0: "no order", 1: "to starboard", 2: "to port", 7: "unavailable"}


def describe(pgn, data):
    """Field-level expansion for the PGNs this project produces."""
    if pgn == 127245 and len(data) >= 6:
        instance = data[0]
        direction = RUDDER_DIRECTION.get(data[1] & 0x07, "reserved")
        order = rad_field_to_deg(data, 2)
        position = rad_field_to_deg(data, 4)
        return "instance={} position={} order={} direction={}".format(
            instance,
            "n/a" if position is None else "{:+.1f} deg".format(position),
            "n/a" if order is None else "{:+.1f} deg".format(order),
            direction,
        )

    if pgn == 60928 and len(data) >= 8:
        name = int.from_bytes(bytes(data[:8]), "little")
        unique = name & 0x1FFFFF
        mfg = (name >> 21) & 0x7FF
        device_function = (name >> 40) & 0xFF
        device_class = (name >> 49) & 0x7F
        return (
            "unique={} manufacturer={} function={} class={}".format(
                unique, mfg, device_function, device_class
            )
        )

    if pgn == 59904 and len(data) >= 3:
        return "requested_pgn={}".format(data[0] | (data[1] << 8) | (data[2] << 16))

    return ""


def parse_rows(handle):
    """Yield (timestamp, can_id, extended, data, direction) from a SavvyCAN CSV."""
    reader = csv.reader(handle)
    cols = None
    for row in reader:
        if len(row) < 5:
            continue

        # Columns are located by header name rather than fixed position:
        # SavvyCAN emits a "Dir" column in some configurations and not others,
        # which shifts everything after it.
        if row[0].strip().lower().startswith("time"):
            names = [cell.strip().lower() for cell in row]
            try:
                cols = {
                    "id": names.index("id"),
                    "extended": names.index("extended"),
                    "len": names.index("len"),
                }
            except ValueError:
                continue
            cols["dir"] = names.index("dir") if "dir" in names else None
            cols["data"] = cols["len"] + 1
            continue

        if cols is None:
            # No header seen: assume the documented layout
            # Time Stamp,ID,Extended,Bus,LEN,D1..D8
            cols = {"id": 1, "extended": 2, "len": 4, "dir": None, "data": 5}

        try:
            timestamp = int(row[0])
            can_id = int(row[cols["id"]], 16)
            extended = row[cols["extended"]].strip().lower() == "true"
            length = int(row[cols["len"]])
        except (ValueError, IndexError):
            continue

        direction = row[cols["dir"]].strip() if cols["dir"] is not None else ""

        data = []
        for cell in row[cols["data"] : cols["data"] + length]:
            cell = cell.strip()
            if cell:
                data.append(int(cell, 16))
        yield timestamp, can_id, extended, data, direction


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logfile", nargs="?", help="SavvyCAN CSV capture (default: stdin)")
    parser.add_argument("--pgn", type=int, action="append",
                        help="only show this PGN (repeatable)")
    parser.add_argument("--src", type=int, action="append",
                        help="only show this source address (repeatable)")
    args = parser.parse_args()

    handle = open(args.logfile, newline="") if args.logfile else sys.stdin
    counts = {}
    shown = 0

    try:
        for timestamp, can_id, extended, data, direction in parse_rows(handle):
            if not extended:
                # NMEA 2000 is extended-ID only; a standard ID is something else
                # sharing the wire.
                continue

            priority, pgn, src, dest = decode_can_id(can_id)
            counts[pgn] = counts.get(pgn, 0) + 1

            if args.pgn and pgn not in args.pgn:
                continue
            if args.src and src not in args.src:
                continue

            name = PGN_NAMES.get(pgn, "")
            detail = describe(pgn, data)
            payload = " ".join("{:02X}".format(b) for b in data)

            print(
                "{:>12}  src={:<3} {}  pri={}  PGN {:<6} {:<22} {:<24} {}".format(
                    timestamp,
                    src,
                    "-> {:<3}".format(dest) if dest != 0xFF else "   bcast",
                    priority,
                    pgn,
                    name,
                    payload,
                    detail,
                )
            )
            shown += 1
    finally:
        if args.logfile:
            handle.close()

    # Keep the summary after the frames when stdout is redirected.
    sys.stdout.flush()
    print("\n{} frames shown. PGN totals:".format(shown), file=sys.stderr)
    for pgn, count in sorted(counts.items(), key=lambda kv: -kv[1]):
        print("  {:<8} {:<26} {}".format(pgn, PGN_NAMES.get(pgn, ""), count),
              file=sys.stderr)


if __name__ == "__main__":
    main()
