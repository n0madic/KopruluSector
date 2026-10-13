"""Terrain resources for Defold: per tileset an indexed megatile tilesource, a 256x256
tilemap template and the mission template collection loaded through a collection proxy
(ADR 0004).

Outputs under <out>/:
  tilesources/<tileset>.png / .tilesource   every megatile, 32x32, one byte palette index per pixel
  templates/<tileset>.tilemap               256x256 template defined by two corner cells (spike 09)
  templates/mission_<tileset>.collection    game object "terrain" with the tilemap component "tilemap"
                                            and the fill script (presentation/terrain.script)
  viewer/<tileset>.mcrc                     u32 CRC-32 of each megatile's 32x32 indices, for the
                                            viewer's byte check of the rendered terrain
  textures.texture_profiles                 shared indexed profiles (converter/emit/profiles.py)

Tile index t + 1 in the tilemap is megatile t (0 means an empty cell).
"""
from __future__ import annotations

import logging
import struct
import zlib
from pathlib import Path

from .. import raster
from ..formats import tileset
from ..formats.palette import TILESETS
from ..mpq.profile import Resolver
from . import profiles
from .atlas import png_bytes
from .defold import project_path, quote, write_if_changed

log = logging.getLogger(__name__)

TEMPLATE = 256
COLUMNS = 120  # 120 * (32 + 2 extruded) = 4080 <= 4096 texels per row
MATERIAL = "/render/materials/terrain.material"
FILL_SCRIPT = "/presentation/terrain.script"


def tile_sheet(ts: tileset.Tileset) -> raster.Image:
    rows = -(-ts.megatile_count // COLUMNS)
    sheet = raster.new("L", (COLUMNS * tileset.MEGATILE, rows * tileset.MEGATILE), 0)
    for i in range(ts.megatile_count):
        tile = raster.frombytes("L", (tileset.MEGATILE, tileset.MEGATILE), ts.render_megatile(i))
        sheet.paste(tile, ((i % COLUMNS) * tileset.MEGATILE, (i // COLUMNS) * tileset.MEGATILE))
    return sheet


def tilesource_text(image: str) -> str:
    return (f'image: "{image}"\ntile_width: 32\ntile_height: 32\ntile_margin: 0\ntile_spacing: 0\n'
            f'collision: ""\nmaterial_tag: "tile"\ncollision_groups: "default"\n'
            f'extrude_borders: 1\ninner_padding: 0\nsprite_trim_mode: SPRITE_TRIM_MODE_OFF\n')


def tilemap_text(tile_set: str, material: str, full: bool) -> str:
    out = [f'tile_set: "{tile_set}"', "layers {", '  id: "terrain"', "  z: 0.0", "  is_visible: 1"]
    coords = ((x, y) for y in range(TEMPLATE) for x in range(TEMPLATE)) if full else ((0, 0), (TEMPLATE - 1, TEMPLATE - 1))
    for x, y in coords:
        out.append(f"  cell {{\n    x: {x}\n    y: {y}\n    tile: 0\n    h_flip: 0\n    v_flip: 0\n  }}")
    out += ["}", f'material: "{material}"', "blend_mode: BLEND_MODE_ALPHA", ""]
    return "\n".join(out)


def template_collection_text(name: str, tilemap: str) -> str:
    component = (f'components {{\n  id: "tilemap"\n  component: {quote(tilemap)}\n}}\n'
                 f'components {{\n  id: "terrain"\n  component: {quote(FILL_SCRIPT)}\n}}\n')
    return (f'name: "mission_{name}"\nscale_along_z: 0\n'
            f'embedded_instances {{\n  id: "terrain"\n  data: {quote(component)}\n}}\n')


def generate(resolver: Resolver, out: Path) -> list[Path]:
    written = []
    for name in TILESETS:
        raw = {ext: resolver.read(f"tileset\\{name}.{ext}")[1] for ext in ("cv5", "vx4", "vr4", "vf4")}
        ts = tileset.decode(raw["cv5"], raw["vx4"], raw["vr4"], raw["vf4"])
        png = out / "tilesources" / f"{name}.png"
        source = png.with_suffix(".tilesource")
        tilemap = out / "templates" / f"{name}.tilemap"
        collection = out / "templates" / f"mission_{name}.collection"
        crcs = [zlib.crc32(ts.render_megatile(i)) for i in range(ts.megatile_count)]
        files = {
            out / "viewer" / f"{name}.mcrc": struct.pack(f"<{len(crcs)}I", *crcs),
            png: png_bytes(tile_sheet(ts)),
            source: tilesource_text(project_path(png)),
            tilemap: tilemap_text(project_path(source), MATERIAL, full=False),
            collection: template_collection_text(name, project_path(tilemap)),
        }
        changed = [p for p, data in files.items() if write_if_changed(p, data)]
        written += files
        log.info("%s %s: %d megatiles", "wrote" if changed else "same ", name, ts.megatile_count)
    return written + [profiles.write(out)]
