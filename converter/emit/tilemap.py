"""Tileset resources for the terrain spike (09): indexed megatile tilesource, tilemap
templates, tileset/mission Koprulu containers.

Outputs under <out>/:
  <tileset>.png / .tilesource   all megatiles, 32x32, one byte palette index per pixel
  <tileset>_corners.tilemap     256x256 template defined by two corner cells only
  <tileset>_full.tilemap        256x256 template with every cell present (comparison)
  <tileset>.kpd                 TSET: CV5M (u16 megatile per group*16+cell), WPE, CYCL
  <map>.kpm                     SPKM (spike-only layout): META (w, h, era u16) + MTXM (raw tile WORDs)
"""
from __future__ import annotations

import hashlib
import logging
import struct
from pathlib import Path

from PIL import Image

from .. import CONVERTER_VERSION
from ..formats import palette, tileset
from ..kpf.container import Container
from ..mpq.profile import Resolver
from . import atlas
from .defold import project_path, write_if_changed

log = logging.getLogger(__name__)

TEMPLATE = 256
COLUMNS = 120  # 120 * (32 + 2 extruded) = 4080 <= 4096 texels per row
PROFILE = "bw-1.17.0.1"
TILESET_SCHEMA = 1
MISSION_SCHEMA = 1

# (output name, campaign directory, crop) for the maps used by the spike.
MAPS = (
    ("desert_terran05a", "campaign\\expterran\\terran05a", None),
    ("install_terran04", "campaign\\terran\\terran04", None),
    ("install_64", "campaign\\terran\\terran04", (64, 64)),
)


def chk_sections(data: bytes) -> dict[bytes, list[bytes]]:
    """Linear CHK section walk (enough for the stock campaign maps, see campaigns.md)."""
    out: dict[bytes, list[bytes]] = {}
    pos = 0
    while pos + 8 <= len(data):
        name, size = struct.unpack_from("<4sI", data, pos)
        out.setdefault(name, []).append(data[pos + 8 : pos + 8 + size])
        pos += 8 + size
    return out


def tile_sheet(ts: tileset.Tileset) -> Image.Image:
    rows = -(-ts.megatile_count // COLUMNS)
    sheet = Image.new("L", (COLUMNS * tileset.MEGATILE, rows * tileset.MEGATILE), 0)
    for i in range(ts.megatile_count):
        tile = Image.frombytes("L", (tileset.MEGATILE, tileset.MEGATILE), ts.render_megatile(i))
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


def tileset_container(name: str, ts: tileset.Tileset, wpe: bytes, source_hash: bytes) -> bytes:
    c = Container(b"TSET", TILESET_SCHEMA, CONVERTER_VERSION, PROFILE, source_hash=source_hash)
    c.add("CV5M", b"".join(struct.pack("<16H", *g) for g in ts.groups))
    c.add("WPE ", wpe)
    ranges = palette.CYCLE_RANGES[name]
    c.add("CYCL", bytes([len(ranges), palette.CYCLE_SPEED]) + b"".join(bytes(r) for r in ranges))
    return c.to_bytes()


def mission_container(width: int, height: int, era: int, mtxm: bytes, source_hash: bytes) -> bytes:
    c = Container(b"SPKM", MISSION_SCHEMA, CONVERTER_VERSION, PROFILE, source_hash=source_hash)
    c.add("META", struct.pack("<3H", width, height, era))
    c.add("MTXM", mtxm)
    return c.to_bytes()


def crop_mtxm(mtxm: bytes, width: int, new_w: int, new_h: int) -> bytes:
    return b"".join(mtxm[(y * width) * 2 : (y * width + new_w) * 2] for y in range(new_h))


def tile_mtxm(mtxm: bytes, width: int, height: int, size: int) -> bytes:
    """Repeat a map to size x size (synthetic worst case for the fill benchmark)."""
    rows = []
    for y in range(size):
        row = mtxm[(y % height) * width * 2 : (y % height + 1) * width * 2]
        rows.append((row * (size // width + 1))[: size * 2])
    return b"".join(rows)


def generate(r: Resolver, out: Path, tilesets: tuple[str, ...] = ("desert", "install"),
             material: str = "/spikes/09-tilemap/index_tilemap.material") -> None:
    out.mkdir(parents=True, exist_ok=True)
    for name in tilesets:
        raw = {ext: r.read(f"tileset\\{name}.{ext}")[1] for ext in ("cv5", "vx4", "vr4", "vf4", "wpe")}
        source_hash = hashlib.sha256(b"".join(raw.values())).digest()
        ts = tileset.decode(raw["cv5"], raw["vx4"], raw["vr4"], raw["vf4"])
        png = out / f"{name}.png"
        if not png.exists():
            tile_sheet(ts).save(png)
        write_if_changed(out / f"{name}.tilesource", tilesource_text(project_path(png)))
        for full in (False, True):
            path = out / f"{name}_{'full' if full else 'corners'}.tilemap"
            write_if_changed(path, tilemap_text(project_path(out / f"{name}.tilesource"), material, full))
        write_if_changed(out / f"{name}.kpd", tileset_container(name, ts, raw["wpe"], source_hash))
        log.info("%s: %d megatiles, sheet %s", name, ts.megatile_count, Image.open(png).size)
    write_if_changed(out / "indexed.texture_profiles", atlas.texture_profiles([(project_path(out) + "/**", "l8")]))
    for map_name, directory, crop in MAPS:
        chk = r.read(directory + "\\staredit\\scenario.chk")[1]
        sections = chk_sections(chk)
        era = struct.unpack("<H", sections[b"ERA "][-1])[0]
        width, height = struct.unpack("<2H", sections[b"DIM "][-1][:4])
        mtxm = sections[b"MTXM"][-1][: width * height * 2]
        if crop:
            mtxm, width, height = crop_mtxm(mtxm, width, *crop), crop[0], crop[1]
        source_hash = hashlib.sha256(chk).digest()
        write_if_changed(out / f"{map_name}.kpm", mission_container(width, height, era, mtxm, source_hash))
        if map_name.startswith("desert"):
            big = tile_mtxm(mtxm, width, height, TEMPLATE)
            write_if_changed(out / "desert_256.kpm", mission_container(TEMPLATE, TEMPLATE, era, big, source_hash))
        log.info("%s: %dx%d era %d (%s)", map_name, width, height, era, palette.TILESETS[era])
