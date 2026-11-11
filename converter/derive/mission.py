"""The terrain state a map's load builds before its doodads and units (`0x490d80`): the walkability grid (walk.py), the
region graph (regions.py, after the console strips of `0x469170`) and the contours (contours.py), from the map's MTXM and
its tileset's CV5 and VF4.

The runtime builds it in C (kp_derive.c, ADR 0018); this is its reference, checked against the native probes
(converter/tests/test_derive.py) and decoded for the C suites' fixture (kpf/fixtures.py)."""
from __future__ import annotations

import functools
from dataclasses import dataclass

from . import contours, regions, terrain, walk


@dataclass(frozen=True)
class Terrain:
    ids: list[int]    # the megatile ids of `0x445720`
    flags: list[int]  # the tile flags of `0x445720`
    walk: bytes       # the walkability grid, a byte per mini-cell
    rgns: bytes       # regions.encode of the graph
    cont: bytes       # contours.encode of the contours


@functools.lru_cache(maxsize=8)
def _megatile_flags(vf4: bytes) -> tuple[int, ...]:
    return tuple(terrain.megatile_flags(vf4))


def build(width: int, height: int, mtxm: bytes, cv5: bytes, vf4: bytes) -> Terrain:
    """The map's terrain state; raises terrain.TerrainError or regions.RegionError when the game cannot load it."""
    ids, flags = terrain.import_terrain(mtxm, width, height, cv5, list(_megatile_flags(vf4)))
    grid = walk.walk_grid(width, height, ids, vf4)
    graph = regions.build(width, height, terrain.edge_strips(width, height, flags), ids, vf4, grid)
    return Terrain(ids, flags, grid, regions.encode(graph), contours.encode(contours.build(width, height, grid)))
