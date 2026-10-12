"""LO overlay offsets (`.lo?` files named by the images.dat overlay columns).

Layout (docs/research/native-image-geometry.md, "LO classes"): u32 frame count, u32 point
count, then one u32 file offset per frame; each frame's points are signed-byte (x, y)
pairs at that offset. The native getters `0x40f5d0`/`0x40f6a0` read them unchecked, so the
converter only verifies that every frame's points lie inside the file.
"""
from __future__ import annotations

import struct

HEADER = struct.Struct("<II")


class LoError(ValueError):
    """Malformed LO data."""


def check(data: bytes) -> tuple[int, int]:
    """(frames, points) after checking that every frame's point pairs fit in `data`."""
    if len(data) < HEADER.size:
        raise LoError("shorter than header")
    frames, points = HEADER.unpack_from(data, 0)
    if HEADER.size + 4 * frames > len(data):
        raise LoError(f"{frames} frame offsets exceed {len(data)} bytes")
    for f in range(frames):
        offset = struct.unpack_from("<I", data, HEADER.size + 4 * f)[0]
        if offset + 2 * points > len(data):
            raise LoError(f"frame {f}: {points} points at {offset:#x} exceed {len(data)} bytes")
    return frames, points


def point(data: bytes, frame: int, index: int) -> tuple[int, int]:
    """Signed (x, y) of point `index` in `frame`, unmirrored."""
    offset = struct.unpack_from("<I", data, HEADER.size + 4 * frame)[0]
    return struct.unpack_from("<bb", data, offset + 2 * index)
