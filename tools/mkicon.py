#!/usr/bin/env python3
"""Write ICON0.PNG, the cover the library shows for this cart.

SPDX-License-Identifier: GPL-2.0-or-later
A hand-rolled PNG writer, so building the cart needs no image library.
"""
import struct, zlib, sys

W, H = 320, 176

def px(x, y):
    # A diagonal gradient with a lighter band, matching the app's own placeholder styling.
    t = (x / W) * 0.6 + (y / H) * 0.4
    r = int(60 + 60 * t)
    g = int(110 + 80 * t)
    b = int(200 + 40 * (1 - t))
    if abs((x * 0.5 + y) - (W * 0.25 + H * 0.5)) < 14:
        r, g, b = min(r + 45, 255), min(g + 45, 255), min(b + 45, 255)
    return r, g, b

def main(path):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            raw.extend(px(x, y))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)
    print("wrote", path)

if __name__ == "__main__":
    main(sys.argv[1])
