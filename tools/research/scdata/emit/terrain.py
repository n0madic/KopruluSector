"""Terrain outputs: the megatile sheet of a tileset (TSET `SHET`, written by scdata/kpf/kpd.py) and the CRCs of its
megatiles for the product's terrain check.

Outputs under <out>/:
  viewer/<tileset>.mcrc   u32 CRC-32 of each megatile's 32x32 indices, for `koprulu --autotest`'s byte check of the
                          drawn terrain
"""
from __future__ import annotations

import logging
import struct
import zlib
from pathlib import Path

from .. import raster
from ..formats import tileset
from ..formats.palette import TILESETS
from ..source import Source
from .files import write_if_changed

log = logging.getLogger(__name__)

SHEET_COLUMNS = 128  # the SHET sheet: 128 * 32 = 4096 texels per row


def tile_sheet(ts: tileset.Tileset, columns: int = SHEET_COLUMNS) -> raster.Image:
    """Every megatile, 32x32 palette indices, megatile t at ((t % columns) * 32, (t // columns) * 32); the rest of the
    last row is 0."""
    rows = -(-ts.megatile_count // columns)
    sheet = raster.new("L", (columns * tileset.MEGATILE, rows * tileset.MEGATILE), 0)
    for i in range(ts.megatile_count):
        tile = raster.frombytes("L", (tileset.MEGATILE, tileset.MEGATILE), ts.render_megatile(i))
        sheet.paste(tile, ((i % columns) * tileset.MEGATILE, (i // columns) * tileset.MEGATILE))
    return sheet


def generate(resolver: Source, out: Path) -> list[Path]:
    written = []
    for name in TILESETS:
        raw = {ext: resolver.read(f"tileset\\{name}.{ext}")[1] for ext in ("cv5", "vx4", "vr4", "vf4")}
        ts = tileset.decode(raw["cv5"], raw["vx4"], raw["vr4"], raw["vf4"])
        crcs = [zlib.crc32(ts.render_megatile(i)) for i in range(ts.megatile_count)]
        path = out / "viewer" / f"{name}.mcrc"
        changed = write_if_changed(path, struct.pack(f"<{len(crcs)}I", *crcs))
        written.append(path)
        log.info("%s %s: %d megatiles", "wrote" if changed else "same ", name, ts.megatile_count)
    return written
