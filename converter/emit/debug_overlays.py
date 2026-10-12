"""Debug overlays for the map viewer (plan §6: walkability and region overlays from phase 1).

From a mission's derived sections it writes RGBA PNGs under generated/viewer/:
- <campaign>_<mission>_walk.png: one pixel per 8x8 mini-cell (WALK); blocked cells red,
  cells with a blocked side (bits 1/2/4/8) amber, walkable cells transparent;
- <campaign>_<mission>_regions.png: one pixel per tile (RGNS raster); a colour per region id
  (fixed hash), split cells (ids >= 0x2000) white, unwalkable class regions dark.
These show the converter's initial terrain state; buildings and doodads change walkability at
runtime (phase 3), which the simulation's own overlays will show.
"""
from __future__ import annotations

import io

from PIL import Image

from ..derive import regions

BLOCKED = (255, 32, 32, 150)
EDGE = (255, 176, 0, 110)
CLEAR = (0, 0, 0, 0)
SPLIT = (255, 255, 255, 140)
UNWALKABLE = (0, 0, 0, 120)
SPLIT_BASE = 0x2000
UNWALKABLE_CLASS = 0x1FFD


def _png(image: Image.Image) -> bytes:
    buf = io.BytesIO()
    image.save(buf, format="PNG", optimize=False)
    return buf.getvalue()


def walk_png(width: int, height: int, walk: bytes) -> bytes:
    w, h = width * 4, height * 4
    out = bytearray(w * h * 4)
    for i, v in enumerate(walk[: w * h]):
        colour = BLOCKED if v & 0x80 else EDGE if v & 0x0F else CLEAR
        out[4 * i : 4 * i + 4] = bytes(colour)
    return _png(Image.frombytes("RGBA", (w, h), bytes(out)))


def region_colour(region: int) -> tuple[int, int, int, int]:
    x = (region * 2654435761) & 0xFFFFFFFF
    return (64 + (x & 0x7F), 64 + ((x >> 8) & 0x7F), 64 + ((x >> 16) & 0x7F), 110)


def regions_png(payload: bytes) -> bytes:
    g = regions.decode(payload)
    out = bytearray(g.width * g.height * 4)
    for i, rid in enumerate(g.raster):
        if rid >= SPLIT_BASE:
            colour = SPLIT
        elif g.classes[rid] == UNWALKABLE_CLASS:
            colour = UNWALKABLE
        else:
            colour = region_colour(rid)
        out[4 * i : 4 * i + 4] = bytes(colour)
    return _png(Image.frombytes("RGBA", (g.width, g.height), bytes(out)))
