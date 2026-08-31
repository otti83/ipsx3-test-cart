#!/usr/bin/env python3
"""Minimal PARAM.SFO writer (PSF v1.1), enough for RPCS3/iPSX3 to list a homebrew folder."""
import struct, sys

FMT_UTF8_S = 0x0004   # UTF-8, not null-terminated (special)
FMT_UTF8   = 0x0204   # UTF-8, null-terminated
FMT_INT32  = 0x0404

def build(entries):
    keys, data, index = b"", b"", b""
    for key, fmt, val, maxlen in entries:
        koff = len(keys)
        keys += key.encode() + b"\0"
        if fmt == FMT_INT32:
            raw = struct.pack("<I", val); dlen = 4; dmax = 4
        else:
            raw = val.encode() + (b"\0" if fmt == FMT_UTF8 else b"")
            dlen = len(raw); dmax = maxlen
            raw = raw.ljust(dmax, b"\0")
        index += struct.pack("<HHIII", koff, fmt, dlen, dmax, len(data))
        data += raw
    keys = keys.ljust((len(keys) + 3) // 4 * 4, b"\0")
    hdr_len = 20 + len(index)
    key_start = hdr_len
    data_start = key_start + len(keys)
    hdr = struct.pack("<4sIIII", b"\x00PSF", 0x00000101, key_start, data_start, len(entries))
    return hdr + index + keys + data

# Keys sorted alphabetically: the format expects the key table in order.
ENTRIES = [
    ("APP_VER",        FMT_UTF8,   "01.00",                          8),
    ("ATTRIBUTE",      FMT_INT32,  0,                                4),
    ("BOOTABLE",       FMT_INT32,  1,                                4),
    ("CATEGORY",       FMT_UTF8,   "HG",                             4),
    ("LICENSE",        FMT_UTF8,   "iPSX3 Test Cart is free software released under GPL-2.0-or-later.", 128),
    ("PARENTAL_LEVEL", FMT_INT32,  0,                                4),
    ("PS3_SYSTEM_VER", FMT_UTF8,   "01.0000",                       8),
    ("RESOLUTION",     FMT_INT32,  63,                               4),
    ("SOUND_FORMAT",   FMT_INT32,  1,                                4),
    ("TITLE",          FMT_UTF8,   "iPSX3 Test Cart",              128),
    ("TITLE_ID",       FMT_UTF8_S, "IPSX30001",                     16),
    ("VERSION",        FMT_UTF8,   "01.00",                          8),
]

if __name__ == "__main__":
    open(sys.argv[1], "wb").write(build(ENTRIES))
    print("wrote", sys.argv[1])
