"""FNT fonts: header, glyph metrics and the 3-bit glyph packets of blitter `0x4cfdf0`.

Layout (docs/research/native-text.md): `FONT`, BYTE low code, high code, max width,
max height, then high-low+1 DWORD offsets from the file start; a nonzero offset points
to BYTE (width, height, x offset, y offset) and packets. A packet's high 5 bits skip
pixels (possibly across rows), the low 3 bits select a colour-row entry. A zero skip
always plots (mask 0 included); a positive skip plots only a nonzero mask and only
inside the glyph.

The effective StarDat `font\\*.fnt` resources are encrypted; their decoded bytes equal
BroodWar.mpq `files\\font\\*.fnt` plus one trailing NUL (docs/research/native-font-decoder.md),
so the converter reads the plaintext copies and appends the NUL.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"FONT"
UNTOUCHED = 0  # glyph mask value of a pixel the blitter does not write


class FntError(ValueError):
    """Malformed FNT data."""


@dataclass(frozen=True)
class Glyph:
    code: int
    offset: int  # file offset of the metrics
    width: int
    height: int
    x_offset: int
    y_offset: int


@dataclass(frozen=True)
class Font:
    low: int
    high: int
    max_width: int
    max_height: int
    offsets: tuple[int, ...]
    glyphs: dict[int, Glyph]


def decode(data: bytes) -> Font:
    if data[:4] != MAGIC:
        raise FntError("missing FONT header")
    low, high, max_w, max_h = data[4:8]
    if high < low:
        raise FntError(f"code range {low}..{high}")
    offsets = struct.unpack_from(f"<{high - low + 1}I", data, 8)
    glyphs = {}
    for i, off in enumerate(offsets):
        if off:
            if off + 4 > len(data):
                raise FntError(f"glyph {low + i} offset {off:#x} out of bounds")
            w, h, x, y = data[off : off + 4]
            glyphs[low + i] = Glyph(low + i, off, w, h, x, y)
    return Font(low, high, max_w, max_h, offsets, glyphs)


def glyph_masks(data: bytes, glyph: Glyph) -> tuple[bytes, int]:
    """(width*height mask values, packet bytes consumed): 0 = untouched, k + 1 = row entry k."""
    out = bytearray(glyph.width * glyph.height)
    pos, at, base = 0, 0, glyph.offset + 4
    while pos < len(out):
        if base + at >= len(data):
            raise FntError(f"glyph {glyph.code}: packets run past the end")
        value = data[base + at]
        at += 1
        skip, mask = value >> 3, value & 7
        pos += skip
        if pos >= len(out):
            break
        if not skip or mask:
            out[pos] = mask + 1
            pos += 1
    return bytes(out), at


def blit(masks: bytes, lookup: bytes, initial: int = UNTOUCHED) -> bytes:
    """Glyph pixels as the native blitter writes them over `initial` with a colour row."""
    return bytes(lookup[m - 1] if m else initial for m in masks)
