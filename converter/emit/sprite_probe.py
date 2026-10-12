"""Sprite probe for the map viewer's web autotest (plan §7 phase 1, item 7).

The autotest reads back the index target and compares isolated static images with the
palette indices of their GRP frames, which catches a wrong texture orientation of the
mesh_texture backend (frames drawn mirrored, upside down, or from the wrong atlas rectangle,
including frames bob packs rotated). The engine cannot read atlas pixels, so the converter
writes the frames of a few animations of the M1 mission (terran/terran01) here:

  viewer/sprite_probe.bin   "KPSP", u16 animation count, then per animation:
                            u8 name length, name (atlas animation id), u16 frame count,
                            per frame: u16 width, u16 height, width*height indices (row-major,
                            top row first, 0 under transparent pixels)
"""
from __future__ import annotations

import struct
from pathlib import Path

from ..kpf import kpd
from . import atlas
from .defold import write_if_changed
from ..errors import ConverterError

MAGIC = b"KPSP"
# Units and doodads of terran01's first view; hdtree01 and min01 are frames bob packs rotated.
ANIMATIONS = (
    "terran_marine",
    "terran_scv",
    "thingy_tileset_badlands_hgtree01",
    "thingy_tileset_badlands_hdtree01",
    "neutral_min01",
)


class ProbeError(ConverterError, RuntimeError):
    """A probe animation is missing from the GRP set."""


def encode(gs: kpd.GrpSet, animations: tuple[str, ...] = ANIMATIONS) -> bytes:
    by_id = {atlas.animation_id(e.path): e for e in gs.grps}
    out = bytearray(MAGIC + struct.pack("<H", len(animations)))
    for name in animations:
        e = by_id.get(name)
        if e is None:
            raise ProbeError(f"probe animation {name} not in the GRP set")
        raw = name.encode("ascii")
        out += struct.pack("<B", len(raw)) + raw + struct.pack("<H", len(e.grp.frames))
        for f in e.grp.frames:
            out += struct.pack("<HH", f.width, f.height) + f.indices
    return bytes(out)


def decode(data: bytes) -> dict[str, list[tuple[int, int, bytes]]]:
    if data[:4] != MAGIC:
        raise ValueError("not a sprite probe")
    (count,) = struct.unpack_from("<H", data, 4)
    at, out = 6, {}
    for _ in range(count):
        n = data[at]
        name = data[at + 1 : at + 1 + n].decode("ascii")
        at += 1 + n
        (frames,) = struct.unpack_from("<H", data, at)
        at += 2
        out[name] = []
        for _ in range(frames):
            w, h = struct.unpack_from("<HH", data, at)
            out[name].append((w, h, data[at + 4 : at + 4 + w * h]))
            at += 4 + w * h
    return out


def write(gs: kpd.GrpSet, out: Path) -> None:
    write_if_changed(out / "viewer" / "sprite_probe.bin", encode(gs))
