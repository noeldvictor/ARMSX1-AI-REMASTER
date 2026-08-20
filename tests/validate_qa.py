#!/usr/bin/env python3
"""Validate a PNG produced by armsx-qa.

The QA driver writes PNGs with a hand-rolled writer (miniz for deflate, chunk
framing and CRCs by hand), so this checks the container is actually well-formed
rather than merely non-empty. A capture an agent cannot open is worse than no
capture at all, because it looks like the run succeeded.
"""
import struct
import sys
import zlib


def fail(msg):
    print(f"ARMSX_QA_PNG failed: {msg}", file=sys.stderr)
    raise SystemExit(1)


def main():
    if len(sys.argv) != 2:
        fail("usage: validate_qa.py <png>")

    path = sys.argv[1]
    try:
        data = open(path, "rb").read()
    except OSError as exc:
        fail(f"cannot read {path}: {exc}")

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        fail("bad PNG signature")

    offset = 8
    chunks = []
    while offset < len(data):
        if offset + 12 > len(data):
            fail("truncated chunk header")
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        tag = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        if len(body) != length:
            fail(f"truncated {tag.decode(errors='replace')} body")
        want = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
        got = zlib.crc32(tag + body) & 0xFFFFFFFF
        if got != want:
            fail(f"CRC mismatch in {tag.decode(errors='replace')}")
        chunks.append((tag.decode(errors="replace"), length, body))
        offset += 12 + length

    tags = [c[0] for c in chunks]
    for required in ("IHDR", "IDAT", "IEND"):
        if required not in tags:
            fail(f"missing {required}")
    if tags[0] != "IHDR" or tags[-1] != "IEND":
        fail("chunk ordering is wrong")

    ihdr = chunks[tags.index("IHDR")][2]
    width, height, depth, colour, comp, filt, inter = struct.unpack(">IIBBBBB", ihdr)
    if depth != 8 or colour != 2:
        fail(f"expected 8-bit truecolour, got depth={depth} colour={colour}")
    if comp or filt or inter:
        fail("unexpected compression/filter/interlace method")
    if width == 0 or height == 0:
        fail("zero-sized image")

    idat = b"".join(c[2] for c in chunks if c[0] == "IDAT")
    try:
        raw = zlib.decompress(idat)
    except zlib.error as exc:
        fail(f"IDAT is not valid zlib: {exc}")

    expected = height * (1 + width * 3)
    if len(raw) != expected:
        fail(f"decompressed {len(raw)} bytes, expected {expected}")

    for y in range(height):
        f = raw[y * (1 + width * 3)]
        if f != 0:
            fail(f"row {y} uses filter {f}; writer only emits None(0)")

    print(f"ARMSX_QA_PNG passed {width}x{height} idat={len(idat)}B raw={len(raw)}B")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
