"""GRP decoder: RLE frames to palette indices plus an opacity mask.

Layout (docs/research/native-image-geometry.md, "GRP frame bounds"): a `<3H` header
(frame_count, canvas_width, canvas_height), then eight-byte frame records `<4BI>`
(x_offset, y_offset, width, height, data_offset) at `6 + frame*8`. `data_offset` is
relative to the file start; the frame payload begins with `height` WORD line offsets
relative to that payload base.

Packets (docs/research/native-pixel-blitters.md, "RLE and clipping semantics"):
`0x80` set: skip `p & 0x7f` pixels (transparent); else `0x40` set: repeat the next
color `p & 0x3f` times; else: `p` literal colors. Transparency is a packet property, so
a color value 0 written by a repeat/literal packet is opaque.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

HEADER = struct.Struct("<3H")
FRAME = struct.Struct("<4BI")

TRANSPARENT = 0
OPAQUE = 255


class GrpError(ValueError):
    """Malformed GRP data."""


@dataclass(frozen=True)
class Frame:
    x: int
    y: int
    width: int
    height: int
    indices: bytes  # width*height palette indices, row-major; 0 under transparent pixels
    mask: bytes  # width*height bytes, OPAQUE or TRANSPARENT

    @property
    def opaque_pixels(self) -> int:
        return self.mask.count(OPAQUE)


@dataclass(frozen=True)
class Grp:
    width: int
    height: int
    frames: tuple[Frame, ...]


def _decode_frame(data: bytes, x: int, y: int, w: int, h: int, base: int) -> Frame:
    indices = bytearray(w * h)
    mask = bytearray(w * h)
    if base + 2 * h > len(data):
        raise GrpError(f"line table at {base:#x} out of bounds")
    for row in range(h):
        pos = base + struct.unpack_from("<H", data, base + 2 * row)[0]
        out = row * w
        px = 0
        while px < w:
            if pos >= len(data):
                raise GrpError(f"row {row} runs past end of data")
            packet = data[pos]
            pos += 1
            if packet & 0x80:
                n = packet & 0x7F
                px += n
                continue
            if packet & 0x40:
                n = packet & 0x3F
                n = min(n, w - px)
                indices[out + px : out + px + n] = bytes([data[pos]]) * n
                pos += 1
            else:
                n = min(packet, w - px)
                if pos + packet > len(data):
                    raise GrpError(f"literal packet in row {row} runs past end of data")
                indices[out + px : out + px + n] = data[pos : pos + n]
                pos += packet
            if n == 0:
                raise GrpError(f"zero-length packet in row {row}")
            mask[out + px : out + px + n] = b"\xff" * n
            px += n
    return Frame(x, y, w, h, bytes(indices), bytes(mask))


def decode(data: bytes) -> Grp:
    if len(data) < HEADER.size:
        raise GrpError("shorter than header")
    count, width, height = HEADER.unpack_from(data, 0)
    if HEADER.size + FRAME.size * count > len(data):
        raise GrpError(f"{count} frame records exceed {len(data)} bytes")
    frames = []
    for i in range(count):
        x, y, w, h, offset = FRAME.unpack_from(data, HEADER.size + FRAME.size * i)
        frames.append(_decode_frame(data, x, y, w, h, offset))
    return Grp(width, height, tuple(frames))


def encode(width: int, height: int, frames: list[Frame]) -> bytes:
    """Encode frames into RLE GRP bytes (used by tests and fixtures, not for originals)."""
    payloads: list[bytes] = []
    for f in frames:
        rows = []
        for r in range(f.height):
            rows.append(_encode_row(f.indices[r * f.width : (r + 1) * f.width], f.mask[r * f.width : (r + 1) * f.width]))
        table = bytearray()
        offset = 2 * f.height
        for row in rows:
            table += struct.pack("<H", offset)
            offset += len(row)
        payloads.append(bytes(table) + b"".join(rows))
    out = bytearray(HEADER.pack(len(frames), width, height))
    cursor = HEADER.size + FRAME.size * len(frames)
    for f, p in zip(frames, payloads):
        out += FRAME.pack(f.x, f.y, f.width, f.height, cursor)
        cursor += len(p)
    for p in payloads:
        out += p
    return bytes(out)


def _run(indices: bytes, mask: bytes, i: int, limit: int) -> int:
    j = i
    while j < len(indices) and j - i < limit and mask[j] == OPAQUE and indices[j] == indices[i]:
        j += 1
    return j - i


def _encode_row(indices: bytes, mask: bytes) -> bytes:
    out = bytearray()
    i, w = 0, len(indices)
    while i < w:
        j = i
        if mask[i] != OPAQUE:
            while j < w and mask[j] != OPAQUE and j - i < 0x7F:
                j += 1
            out.append(0x80 | (j - i))
        elif (n := _run(indices, mask, i, 0x3F)) >= 3:
            j = i + n
            out += bytes((0x40 | n, indices[i]))
        else:
            while j < w and mask[j] == OPAQUE and j - i < 0x3F and (j == i or _run(indices, mask, j, 3) < 3):
                j += 1
            out.append(j - i)
            out += indices[i:j]
        i = j
    return bytes(out)
