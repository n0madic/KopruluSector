"""GRP decoder: RLE frames to palette indices plus an opacity mask.

Layout (docs/research/native-image-geometry.md, "GRP frame bounds"): a `<3H` header
(frame_count, canvas_width, canvas_height), then eight-byte frame records `<4BI>`
(x_offset, y_offset, width, height, data_offset) at `6 + frame*8`. `data_offset` is
relative to the file start; the frame payload begins with `height` WORD line offsets
relative to that payload base. The native loader (`0x40ff40`) masks the offset with
`0x7fffffff` before turning it into a pointer, and so does `frame_record`.

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
OFFSET_MASK = 0x7FFFFFFF

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
    frames = [_decode_frame(data, *frame_record(data, i)) for i in range(count)]
    return Grp(width, height, tuple(frames))


def frame_record(data: bytes, frame: int) -> tuple[int, int, int, int, int]:
    """(x, y, width, height, payload offset) of a frame, the offset masked as natively."""
    x, y, w, h, offset = FRAME.unpack_from(data, HEADER.size + FRAME.size * frame)
    return x, y, w, h, offset & OFFSET_MASK


# Opacity runs (grpmeta.kpd): the packet structure of a frame without colours, enough for
# the native hit test `IsGrpPixelOpaque` 0x40fd50. One byte per run: bit 7 transparent,
# bits 0-6 length.
RUN_TRANSPARENT = 0x80
RUN_MAX = 0x7F


def row_packets(data: bytes, base: int, row: int, width: int) -> list[tuple[bool, int]]:
    """(transparent, length) of every packet of one row, as the decoder walks it (until the
    row covers `width`)."""
    if base + 2 * row + 2 > len(data):
        raise GrpError(f"line table at {base:#x} out of bounds")
    pos = base + struct.unpack_from("<H", data, base + 2 * row)[0]
    out, px = [], 0
    while px < width:
        if pos >= len(data):
            raise GrpError(f"row {row} runs past end of data")
        packet = data[pos]
        pos += 1
        if packet & 0x80:
            n, transparent = packet & 0x7F, True
        elif packet & 0x40:
            n, transparent = packet & 0x3F, False
            pos += 1
        else:
            n, transparent = packet, False
            pos += n
        if pos > len(data):
            raise GrpError(f"packet in row {row} runs past end of data")
        out.append((transparent, n))
        px += n
    return out


def canonical_runs(packets: list[tuple[bool, int]]) -> bytes:
    """Encode a row's packets as opacity runs that give the same `0x40fd50` result for every x.

    The native loop reads packets and subtracts their lengths from x while x > 0, then
    returns the class of the last packet read. For x > 0 that is the packet whose cumulative
    end is the first one >= x, so merging adjacent packets of one class, dropping empty
    ones and splitting runs at RUN_MAX change nothing. For x = 0 it is the first packet,
    whatever its length: a leading empty packet of the other class is therefore kept.
    """
    merged: list[list] = []
    for transparent, n in packets:
        if n == 0:
            continue
        if merged and merged[-1][0] == transparent:
            merged[-1][1] += n
        else:
            merged.append([transparent, n])
    out = bytearray()
    if packets and (not merged or merged[0][0] != packets[0][0]):
        out.append(RUN_TRANSPARENT if packets[0][0] else 0)
    for transparent, n in merged:
        while n > 0:
            chunk = min(n, RUN_MAX)
            out.append((RUN_TRANSPARENT if transparent else 0) | chunk)
            n -= chunk
    return bytes(out)


def opacity_block(data: bytes, frame: int) -> bytes:
    """A frame's opacity runs: `height` u16 row offsets relative to the block start, then
    the canonical runs of every row."""
    _, _, w, h, base = frame_record(data, frame)
    rows = [canonical_runs(row_packets(data, base, r, w)) for r in range(h)]
    table, offset = bytearray(), 2 * h
    for row in rows:
        if offset > 0xFFFF:
            raise GrpError(f"frame {frame}: opacity runs exceed 64 KiB")
        table += struct.pack("<H", offset)
        offset += len(row)
    return bytes(table) + b"".join(rows)


def pixel_opaque(data: bytes, frame: int, x: int, y: int) -> int:
    """`IsGrpPixelOpaque` 0x40fd50 on raw GRP bytes: 0 when x or y is out of the frame
    (unsigned compare), else the class of the packet at which the do-while loop
    `x -= length while x > 0` stops (1 opaque, 0 transparent)."""
    _, _, w, h, base = frame_record(data, frame)
    x, y = x & 0xFFFFFFFF, y & 0xFFFFFFFF
    if x >= w or y >= h:
        return 0
    pos = base + struct.unpack_from("<H", data, base + 2 * y)[0]
    while True:
        packet = data[pos]
        pos += 1
        if packet & 0x80:
            x -= packet & 0x7F
        elif packet & 0x40:
            x -= packet & 0x3F
            pos += 1
        else:
            x -= packet
            pos += packet
        if x <= 0:
            return 0 if packet & 0x80 else 1


def runs_pixel_opaque(block: bytes, offset: int, width: int, height: int, x: int, y: int) -> int:
    """`0x40fd50` on an opacity block (see `opacity_block`) starting at `offset`."""
    x, y = x & 0xFFFFFFFF, y & 0xFFFFFFFF
    if x >= width or y >= height:
        return 0
    pos = offset + struct.unpack_from("<H", block, offset + 2 * y)[0]
    while True:
        run = block[pos]
        pos += 1
        x -= run & RUN_MAX
        if x <= 0:
            return 0 if run & RUN_TRANSPARENT else 1


def row_hits(runs: list[tuple[bool, int]], width: int) -> bytes:
    """`0x40fd50` for every x of one row at once (1/0 per x), from (transparent, length)
    packets or runs. x = 0 takes the first packet; x > 0 the first packet whose cumulative
    end reaches x."""
    out = bytearray(width)
    if width == 0:
        return bytes(out)
    out[0] = 0 if runs[0][0] else 1
    end = 0
    for transparent, n in runs:
        start, end = end, end + n
        for x in range(max(start + 1, 1), min(end, width - 1) + 1):
            out[x] = 0 if transparent else 1
    return bytes(out)


def block_rows(block: bytes, offset: int, height: int) -> list[list[tuple[bool, int]]]:
    """The (transparent, length) runs of every row of an opacity block (rows end where the
    next row's runs begin; the last row ends at the end of `block`)."""
    starts = [offset + struct.unpack_from("<H", block, offset + 2 * r)[0] for r in range(height)]
    ends = starts[1:] + [len(block)]
    return [[(bool(b & RUN_TRANSPARENT), b & RUN_MAX) for b in block[s:e]] for s, e in zip(starts, ends)]


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
