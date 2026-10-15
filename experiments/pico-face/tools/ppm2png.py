"""Converts a binary PPM to PNG using only the standard library.

Pillow is not a dependency of this repository, and one screenful of zlib and
struct is cheaper than adding one.
"""

import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()

    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while data[pos : pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        if pos == len(data):
            raise SystemExit("truncated PPM header")
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        if pos == len(data):
            raise SystemExit("truncated PPM header")
        fields.append(data[start:pos])

    if fields[0] != b"P6":
        raise SystemExit("only binary PPM (P6) is supported")
    try:
        width, height, maximum = map(int, fields[1:])
    except ValueError:
        raise SystemExit("invalid PPM header") from None
    if width <= 0 or height <= 0 or maximum != 255:
        raise SystemExit("PPM requires positive dimensions and a maximum value of 255")

    pixels = data[pos + 1 :]
    size = width * height * 3
    if len(pixels) < size:
        raise SystemExit("truncated PPM pixels")
    return width, height, pixels[:size]


def write_png(path, width, height, rgb):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride : (y + 1) * stride]

    def chunk(tag, payload):
        body = tag + payload
        return (
            struct.pack(">I", len(payload))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    header = struct.pack(">2I5B", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    w, h, pixels = read_ppm(sys.argv[1])
    write_png(sys.argv[2], w, h, pixels)
