"""Palettes: WPE (tileset palettes) and palette cycling tables.

WPE is 256 entries of (R, G, B, 0) (docs/research/evidence/terrain-assets.json records
the fourth byte as zero for every stock tileset).
"""
from __future__ import annotations

from dataclasses import dataclass

from ..errors import ConverterError

WPE_SIZE = 1024
COLORS = 256

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
