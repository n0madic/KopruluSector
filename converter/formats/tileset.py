"""Tileset decoder: CV5 groups, VX4 megatiles, VR4 minitile pixels, VF4 minitile flags.

Record sizes follow the stock files recorded in docs/research/evidence/terrain-assets.json:
CV5 52 bytes (20-byte group header + 16 megatile WORDs), VX4 and VF4 16 WORDs per
megatile, VR4 64 bytes (8x8 indices) per minitile. A VX4 WORD is `minitile << 1 | hflip`.
An MTXM tile WORD selects CV5 group `(tile >> 4) & 0x7ff` and cell `tile & 15`.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

CV5_RECORD = 52
CV5_HEADER = 20
VX4_RECORD = 32
VF4_RECORD = 32
VR4_RECORD = 64
MEGATILE = 32
MINITILE = 8


class TilesetError(ValueError):
    """Malformed tileset data."""


@dataclass(frozen=True)
class Tileset:
    groups: tuple[tuple[int, ...], ...]  # CV5: 16 megatile ids per group
    group_flags: tuple[int, ...]  # CV5 WORD at offset 2
    megatiles: tuple[tuple[int, ...], ...]  # VX4: 16 WORDs per megatile
    minitile_flags: tuple[tuple[int, ...], ...]  # VF4: 16 WORDs per megatile
    minitiles: bytes  # VR4 raw

    @property
    def megatile_count(self) -> int:
        return len(self.megatiles)

    def megatile_for_tile(self, tile: int) -> int:
        group = (tile >> 4) & 0x7FF
        if group >= len(self.groups):
            return 0
        return self.groups[group][tile & 15]

    def render_megatile(self, index: int) -> bytes:
        """Return 32x32 palette indices of a megatile, row-major."""
        out = bytearray(MEGATILE * MEGATILE)
        refs = self.megatiles[index]
        for cell, ref in enumerate(refs):
            mini, flip = ref >> 1, ref & 1
            src = mini * VR4_RECORD
            if src + VR4_RECORD > len(self.minitiles):
                raise TilesetError(f"megatile {index} references minitile {mini} out of range")
            ox, oy = (cell & 3) * MINITILE, (cell >> 2) * MINITILE
            for y in range(MINITILE):
                row = self.minitiles[src + y * MINITILE : src + (y + 1) * MINITILE]
                if flip:
                    row = row[::-1]
                o = (oy + y) * MEGATILE + ox
                out[o : o + MINITILE] = row
        return bytes(out)


def _words(data: bytes, record: int, what: str) -> list[tuple[int, ...]]:
    if len(data) % record:
        raise TilesetError(f"{what}: size {len(data)} is not a multiple of {record}")
    return list(struct.iter_unpack(f"<{record // 2}H", data))


def decode(cv5: bytes, vx4: bytes, vr4: bytes, vf4: bytes) -> Tileset:
    if len(cv5) % CV5_RECORD:
        raise TilesetError(f"CV5: size {len(cv5)} is not a multiple of {CV5_RECORD}")
    if len(vr4) % VR4_RECORD:
        raise TilesetError(f"VR4: size {len(vr4)} is not a multiple of {VR4_RECORD}")
    groups, flags = [], []
    for off in range(0, len(cv5), CV5_RECORD):
        flags.append(struct.unpack_from("<H", cv5, off + 2)[0])
        groups.append(struct.unpack_from("<16H", cv5, off + CV5_HEADER))
    megatiles = _words(vx4, VX4_RECORD, "VX4")
    vf = _words(vf4, VF4_RECORD, "VF4")
    if len(vf) != len(megatiles):
        raise TilesetError(f"VF4 has {len(vf)} records, VX4 {len(megatiles)}")
    return Tileset(tuple(groups), tuple(flags), tuple(megatiles), tuple(vf), bytes(vr4))
