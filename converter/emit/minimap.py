"""Minimap terrain prerender: `generated/missions/<campaign>/<mission>_minimap.png`.

Follows the original console minimap (atlas P24/P22):
- size by the larger map side (`LayoutMinimap` 0x46d850): 64 -> 2W x 2H; 96/128 -> W x H;
  192/256 -> W/2 x H/2;
- colour of a tile = pixel (7, 6) of a minitile of its megatile (VX4 word & 0xfffe -> VR4
  record, the flip bit ignored): minitile 0 (`0x46c920`), every second tile and row for the
  half size (`0x46c990`), minitiles 0, 1, 4, 5 as a 2 x 2 block for 64-tile maps (`0x46ca20`);
- palette map `0x46e260`: index i stays i unless i is in a palette-cycling range; then the
  nearest static colour `0x4666c0` (indices 1..255 outside the cycling ranges, smallest squared
  RGB distance, first wins, 0 when none).
The PNG is 8-bit indexed with the tileset palette; the indices are the minimap's own.
"""
from __future__ import annotations

from PIL import Image

from ..formats import palette, tileset
from .atlas import png_bytes

PIXEL = 6 * 8 + 7  # byte 0x37 of an 8x8 VR4 minitile: x 7, y 6


def cycling(name: str) -> set[int]:
    out = set()
    for low, high in palette.CYCLE_RANGES[name]:
        out.update(range(low, high + 1))
    return out


def palette_map(rgb: list[tuple[int, int, int]], cycle: set[int]) -> list[int]:
    out = []
    for i, (r, g, b) in enumerate(rgb):
        if i not in cycle:
            out.append(i)
            continue
        best, best_d = 0, 0xFFFFFFFF
        for j in range(1, 256):
            if j in cycle:
                continue
            jr, jg, jb = rgb[j]
            d = (jr - r) ** 2 + (jg - g) ** 2 + (jb - b) ** 2
            if d < best_d:
                best, best_d = j, d
        out.append(best)
    return out


def render(ids: list[int], width: int, height: int, ts: tileset.Tileset, colours: list[int]) -> tuple[int, int, bytes]:
    """(minimap width, height, colour indices) from the imported megatile ids."""
    def colour(mega: int, cell: int) -> int:
        mini = ts.megatiles[mega & 0x7FFF][cell] & 0xFFFE  # minitile * 2; flip bit ignored
        return colours[ts.minitiles[(mini >> 1) * tileset.VR4_RECORD + PIXEL]]

    side = max(width, height)
    if side == 64:
        w, h = 2 * width, 2 * height
        out = bytearray(w * h)
        for ty in range(height):
            for tx in range(width):
                mega = ids[ty * width + tx]
                for (dx, dy), cell in zip(((0, 0), (1, 0), (0, 1), (1, 1)), (0, 1, 4, 5)):
                    out[(2 * ty + dy) * w + 2 * tx + dx] = colour(mega, cell)
        return w, h, bytes(out)
    if side in (96, 128):
        return width, height, bytes(colour(ids[i], 0) for i in range(width * height))
    if side in (192, 256):
        w, h = width // 2, height // 2
        return w, h, bytes(colour(ids[2 * y * width + 2 * x], 0) for y in range(h) for x in range(w))
    raise ValueError(f"map side {side} has no native minimap layout (the original keeps the previous one)")


def png(width: int, height: int, pixels: bytes, rgb: list[tuple[int, int, int]]) -> bytes:
    image = Image.frombytes("P", (width, height), pixels)
    image.putpalette([c for entry in rgb for c in entry])
    return png_bytes(image)


def tileset_rgb(wpe: bytes) -> list[tuple[int, int, int]]:
    return [tuple(wpe[4 * i : 4 * i + 3]) for i in range(256)]  # type: ignore[misc]
