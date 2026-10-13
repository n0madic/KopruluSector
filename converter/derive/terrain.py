"""Terrain import: megatile flags (`0x445562`) and MTXM/CV5 import (`0x445720`).

Contracts: docs/research/native-region-generation.md (megatile flags) and
docs/research/native-terrain-import.md (import). The same rules run in Lua
(game/data/terrain.lua); this implementation is checked against the native output
hashes of all eight tilesets and all 68 campaign CHKs.
"""
from __future__ import annotations

import struct

from ..errors import ConverterError

MFLAGS_ALGORITHM = b"MFL1"
MFLAGS_VERSION = 1

WALKABLE = 0x0001
MIDDLE = 0x0002
HIGH = 0x0004
VERY_HIGH = 0x0008

FLAG_WALKABLE = 0x00010000
FLAG_UNWALKABLE = 0x00040000
FLAG_PARTIAL_WALK = 0x20000000
FLAG_HIGH = 0x04000000
FLAG_MIDDLE = 0x02000000
FLAG_VERY_HIGH = 0x01000000
CV5_FLAG_MASK = 0xD8FA
FLAG_TILE_HIGH_BIT = 0x00400000
INITIAL_LOW_WORD = 0xFFFF  # visibility/exploration bits set at terrain allocation (0x46c1e0)


class TerrainError(ConverterError, ValueError):
    """The map references a CV5 group outside the tileset (the native import stops)."""


def megatile_flags(vf4: bytes) -> list[int]:
    """One DWORD per VF4 record (16 WORDs) by the `0x445562` counting rules."""
    out = []
    for words in struct.iter_unpack("<16H", vf4):
        walk = sum(1 for w in words if w & WALKABLE)
        high = sum(1 for w in words if w & HIGH)
        middle = sum(1 for w in words if w & MIDDLE)
        flags = FLAG_WALKABLE if walk > 12 else FLAG_UNWALKABLE
        if 0 < walk < 16:
            flags |= FLAG_PARTIAL_WALK
        if high >= 12:
            flags |= FLAG_HIGH
        elif middle + high >= 12:
            flags |= FLAG_MIDDLE
        if any(w & VERY_HIGH for w in words):
            flags |= FLAG_VERY_HIGH
        out.append(flags)
    return out


def import_terrain(mtxm: bytes, width: int, height: int, cv5: bytes, mflags: list[int]) -> tuple[list[int], list[int]]:
    """(megatile ids, tile flags) for a map, row-major, as `0x445720` writes them.

    Tile flags start with the low WORD 0xffff set at terrain allocation. A group out of
    range raises: the original returns failure there and the map does not load.
    """
    groups = len(cv5) // 52
    ids, flags = [], []
    for i, raw in enumerate(struct.unpack_from(f"<{width * height}H", mtxm)):
        group = (raw >> 4) & 0x7FF
        if group >= groups:
            raise TerrainError(f"cell {i}: tile {raw:#06x} selects CV5 group {group} of {groups}")
        mega = struct.unpack_from("<H", cv5, group * 52 + 20 + (raw & 15) * 2)[0]
        cv5_flags = struct.unpack_from("<H", cv5, group * 52 + 2)[0]
        f = INITIAL_LOW_WORD | ((cv5_flags & CV5_FLAG_MASK) << 16) | mflags[mega & 0x7FFF]
        if raw & 0x8000:
            mega |= 0x8000
            f |= FLAG_TILE_HIGH_BIT
        ids.append(mega)
        flags.append(f)
    return ids, flags


EDGE_CLEAR, EDGE_SET = 0x20410000, 0x00800000


def edge_strips(width: int, height: int, flags: list[int]) -> list[int]:
    """The tile flags after `LoadTileMapSection 0x469170` blocks the strips behind the console
    (bottom corners 5 x 2 and the bottom row): each rectangle first clears 0x20410000, then
    sets 0x00800000. The game builds its regions (`0x481d50`) from these flags."""
    out = list(flags)
    for x0, y0, w, h in ((0, height - 2, 5, 2), (width - 5, height - 2, 5, 2), (0, height - 1, width, 1)):
        for y in range(max(y0, 0), min(y0 + h, height)):
            for x in range(max(x0, 0), min(x0 + w, width)):
                i = y * width + x
                out[i] = (out[i] & ~EDGE_CLEAR & 0xFFFFFFFF) | EDGE_SET
    return out

