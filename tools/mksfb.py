#!/usr/bin/env python3
"""Write PS3_DISC.SFB, the marker a PS3 disc image carries at its root.

SPDX-License-Identifier: GPL-2.0-or-later

RPCS3 validates the ".SFB" magic before it will treat a directory or image as a disc
(Emu/System.cpp IsValidSfb), so an .iso without one is rejected. The key/value table below is the
ordinary layout: 0x20-byte header, then 0x20-byte entries of {key[0x10], offset u32, size u32}.
"""
import struct, sys

ENTRIES = [("HYBRID_FLAG", b"g"), ("TITLE_ID", b"IPSX30001"), ("VERSION", b"01.00")]

def main(path):
    n = len(ENTRIES)
    tbl_off = 0x20
    data_off = 0x200                       # values live well past the table, as real discs do
    out = bytearray(0x800)                 # one ISO sector
    struct.pack_into(">4sI", out, 0, b".SFB", 1)
    cur = data_off
    for i, (k, v) in enumerate(ENTRIES):
        struct.pack_into(">16sII", out, tbl_off + i * 0x20, k.encode(), cur, len(v))
        out[cur:cur + len(v)] = v
        cur += (len(v) + 0x1F) & ~0x1F
    open(path, "wb").write(bytes(out))
    print("wrote", path)

if __name__ == "__main__":
    main(sys.argv[1])
