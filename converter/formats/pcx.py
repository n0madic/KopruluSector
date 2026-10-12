"""Minimal PCX decoder for 8-bit single-plane images with a trailing 256-color palette."""
from __future__ import annotations

import struct
from dataclasses import dataclass

from ..errors import ConverterError

HEADER_SIZE = 128
PALETTE_MARKER = 0x0C


class PcxError(ConverterError, ValueError):
    """Unsupported or malformed PCX data."""


@dataclass(frozen=True)
class Pcx:
    width: int
    height: int
    pixels: bytes  # width*height indices
    palette: tuple[tuple[int, int, int], ...] | None


def decode(data: bytes) -> Pcx:
    if len(data) < HEADER_SIZE or data[0] != 0x0A:
        raise PcxError("not a PCX file")
    encoding, bpp = data[2], data[3]
    xmin, ymin, xmax, ymax = struct.unpack_from("<4H", data, 4)
    planes = data[65]
    stride = struct.unpack_from("<H", data, 66)[0]
    if encoding != 1 or bpp != 8 or planes != 1:
        raise PcxError(f"unsupported PCX: encoding {encoding}, bpp {bpp}, planes {planes}")
    width, height = xmax - xmin + 1, ymax - ymin + 1
    out = bytearray()
    pos, need = HEADER_SIZE, stride * height
    while len(out) < need:
        if pos >= len(data):
            raise PcxError("pixel data truncated")
        b = data[pos]
        pos += 1
        if b >= 0xC0:
            out += bytes([data[pos]]) * (b & 0x3F)
            pos += 1
        else:
            out.append(b)
    pixels = b"".join(bytes(out[r * stride : r * stride + width]) for r in range(height))
    palette = None
    if len(data) >= 769 and data[-769] == PALETTE_MARKER:
        raw = data[-768:]
        palette = tuple((raw[i], raw[i + 1], raw[i + 2]) for i in range(0, 768, 3))
    return Pcx(width, height, pixels, palette)


def encode(width: int, height: int, pixels: bytes) -> bytes:
    """8-bit RLE PCX with a zero palette (fixtures and tests; originals are only decoded)."""
    header = bytearray(HEADER_SIZE)
    header[0:4] = bytes((0x0A, 5, 1, 8))
    struct.pack_into("<4H", header, 4, 0, 0, width - 1, height - 1)
    header[65] = 1
    struct.pack_into("<H", header, 66, width)
    body = bytearray()
    for row in range(height):
        line = pixels[row * width : (row + 1) * width]
        i = 0
        while i < width:
            run = 1
            while i + run < width and run < 0x3F and line[i + run] == line[i]:
                run += 1
            if run > 1 or line[i] >= 0xC0:
                body += bytes((0xC0 | run, line[i]))
            else:
                body.append(line[i])
            i += run
    return bytes(header) + bytes(body) + bytes([PALETTE_MARKER]) + bytes(768)
