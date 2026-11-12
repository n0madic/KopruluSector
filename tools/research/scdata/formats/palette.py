"""Palettes: WPE (tileset palettes), palette cycling tables and a colour's index in a palette.

WPE is 256 entries of (R, G, B, 0) (docs/research/evidence/terrain-assets.json records
the fourth byte as zero for every stock tileset).

IndexMap is the rule StarCraft: Remastered's RGBA images are brought back to 1.17's palette-indexed files with
(casc/rebuild.py, formats/kpw.py; the C of it is kp_core's kp_index_map.h):
- alpha 0 is index 0 (transparent);
- an opaque pixel takes the lowest index of its colour, a translucent one (the popups' 170) the highest, which
  is how 1.17's files use their duplicate colours (measured on 173 images: 154 exact);
- a colour the palette lacks (DXT's rounding) takes the nearest one (the least squared distance, the lowest index of
  equals).
"""
from __future__ import annotations

from dataclasses import dataclass

from ..errors import ConverterError

WPE_SIZE = 1024
COLORS = 256
TRANSPARENT = 0
OPAQUE = 255
Colours = tuple[tuple[int, int, int], ...]  # 256 x (R, G, B)

# Tileset id (ERA) -> resource name, docs/research/native-palette-bootstrap.md.
TILESETS = ("badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight")

# Stock simple cycle ranges (inclusive), speed 8, from the initialized PE definitions
# (native-palette-bootstrap.md). Platform and Installation have none.
CYCLE_SPEED = 8
_BADLANDS_JUNGLE = ((1, 6), (7, 13), (248, 254))
_ASHWORLD = ((1, 4), (5, 8), (9, 12))
_DESERT_ICE_TWILIGHT = ((1, 13), (248, 254))
CYCLE_RANGES: dict[str, tuple[tuple[int, int], ...]] = {
    "badlands": _BADLANDS_JUNGLE,
    "platform": (),
    "install": (),
    "ashworld": _ASHWORLD,
    "jungle": _BADLANDS_JUNGLE,
    "desert": _DESERT_ICE_TWILIGHT,
    "ice": _DESERT_ICE_TWILIGHT,
    "twilight": _DESERT_ICE_TWILIGHT,
}


class PaletteError(ConverterError, ValueError):
    """Malformed palette data."""


@dataclass(frozen=True)
class Palette:
    rgb: tuple[tuple[int, int, int], ...]

    def to_rgba(self) -> bytes:
        return b"".join(bytes((r, g, b, 255)) for r, g, b in self.rgb)


def decode_wpe(data: bytes) -> Palette:
    if len(data) != WPE_SIZE:
        raise PaletteError(f"WPE must be {WPE_SIZE} bytes, got {len(data)}")
    return Palette(tuple((data[i], data[i + 1], data[i + 2]) for i in range(0, WPE_SIZE, 4)))


def rotate_simple(colors: list, low: int, high: int) -> None:
    """Native simple cycle 0x4cccf0: rotate inclusive low..high right by one entry."""
    last = colors[high]
    colors[low + 1 : high + 1] = colors[low:high]
    colors[low] = last


class IndexMap:
    """RGBA -> palette index by the rule above, memoised per colour."""

    def __init__(self, palette: Colours):
        self.palette = palette
        self.lowest: dict[tuple[int, int, int], int] = {}
        self.highest: dict[tuple[int, int, int], int] = {}
        for i, c in enumerate(palette):
            self.lowest.setdefault(c, i)
            self.highest[c] = i
        self.nearest: dict[tuple[int, int, int], int] = {}

    def __call__(self, r: int, g: int, b: int, a: int) -> int:
        if a == 0:
            return TRANSPARENT
        c = (r, g, b)
        found = (self.lowest if a == OPAQUE else self.highest).get(c)
        if found is not None:
            return found
        near = self.nearest.get(c)
        if near is None:
            near = self.nearest[c] = min(range(len(self.palette)), key=lambda i: (
                (self.palette[i][0] - r) ** 2 + (self.palette[i][1] - g) ** 2 + (self.palette[i][2] - b) ** 2, i))
        return near
