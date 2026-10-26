"""StarCraft: Remastered's HD art for the HD renderer (ADR 0013, stage R2): `generated/hd/<variant>/`.

`hd2` (×2) and `hd` (×4) are written alike from the user's install, never shipped:
- `packs/<pack>.kpd` (`HDPK`), one per SD pack (`generated/packs/<pack>.kpd`), its frames in the SD pack's FRMS
  order, so an SD frame's index is its HD frame's: HEAD (u8 scale, u16 page size, u16 pages, u32 frames), FRMS (per
  frame u16 page, x, y, width, height of the HD frame in its page, i16 dx, dy: its centre less the SD frame's centre
  times the scale, in half HD pixels, y down; width 0: no HD frame), DIFF (the pages' BC3 blocks: the diffuse
  sheets), TEAM (their BC1 blocks: the team colour masks, zero where an image has none). An image's frames come from
  its own anim (`anim/main_<image>.anim`) or the one `SD/images.rel` names; a GRP takes the first image naming it.
  The sheets keep their blocks, packed whole into pages;
- `tilesets/<name>.kpd` (`HDTS`): HEAD (u16 tile count: the tileset's 1.17 megatiles, u16 tile size, u32 FourCC),
  TILE (each megatile's BC1 blocks, tile i is megatile i of `.vf4`/`.vx4`: Remastered's appended tiles left out).

The frames are checked against the GRPs (their counts), the tiles against the megatiles (their count).
"""
from __future__ import annotations

import logging
import struct
from pathlib import Path

from .. import CONVERTER_VERSION
from ..casc.storage import CascStorage
from ..errors import ConverterError
from ..formats import anim, dat, dds
from ..formats.palette import TILESETS
from ..kpf import kpd
from ..kpf.container import Container
from ..kpf.resources import pack_entries
from ..source import Source
from .files import write_if_changed

log = logging.getLogger(__name__)

VARIANTS = {"hd2": ("HD2/", 2), "hd": ("", 4)}
PAGE_SIZE = {"hd2": 4096, "hd": 8192}  # a sheet is at most 4096 (8192) pixels on a side
PACK_KIND, TILESET_KIND = b"HDPK", b"HDTS"
SCHEMA = 1
FRAME = struct.Struct("<HHHHHhh")
BLOCK = 4
DIFFUSE_BLOCK, TEAM_BLOCK = 16, 8  # bytes of a BC3 and a BC1 block
MEGATILE_VF4 = 32  # bytes of a megatile's flags in .vf4
TILESET_FILES = {"ashworld": "AshWorld", "badlands": "badlands", "desert": "Desert", "ice": "Ice",
                 "install": "install", "jungle": "Jungle", "platform": "platform", "twilight": "Twilight"}


class HdError(ConverterError, ValueError):
    """Remastered's HD art does not match the original's images or terrain."""


def blocks(sheet: bytes) -> tuple[bytes, bytes]:
    """(FourCC, block data) of a DDS sheet: its first level without the header."""
    h = dds.header(sheet)
    if h.fourcc not in (b"DXT1", b"DXT5"):
        raise HdError(f"HD sheet in {h.fourcc!r}, BC1 or BC3 expected")
    step = TEAM_BLOCK if h.fourcc == b"DXT1" else DIFFUSE_BLOCK
    size = ((h.width + 3) // 4) * ((h.height + 3) // 4) * step
    data = sheet[dds.HEADER_SIZE:dds.HEADER_SIZE + size]
    if len(data) != size:
        raise HdError("HD sheet truncated")
    return h.fourcc, data


def shelves(sizes: list[tuple[int, int]], page: int) -> tuple[list[tuple[int, int, int]], int]:
    """(page, x, y) of each w × h rectangle (multiples of 4), tallest first along shelves; and the page count."""
    order = sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0], i))
    at: list[tuple[int, int, int]] = [(0, 0, 0)] * len(sizes)
    p = x = y = shelf = 0
    used = False
    for i in order:
        w, h = sizes[i]
        if w > page or h > page:
            raise HdError(f"a {w} x {h} sheet does not fit a {page} page")
        if x + w > page:
            x, y, shelf = 0, y + shelf, 0
        if y + h > page:
            p, x, y, shelf = p + 1, 0, 0, 0
        at[i] = (p, x, y)
        x += w
        shelf = max(shelf, h)
        used = True
    return at, p + 1 if used else 0


def blit_blocks(page: bytearray, page_size: int, x: int, y: int, w: int, h: int, data: bytes, step: int) -> None:
    columns, row = page_size // BLOCK, (w // BLOCK) * step
    for by in range(h // BLOCK):
        to = ((y // BLOCK + by) * columns + x // BLOCK) * step
        page[to:to + row] = data[by * row:(by + 1) * row]


def hd_images(source: Source, gs: kpd.GrpSet, storage: CascStorage, prefix: str) -> dict[int, int]:
    """GRP entry -> the image whose anim draws it (its first image's own anim, else `images.rel`'s)."""
    layout = next(f for f in dat.load_layouts() if f.table == "images")
    grp_files = dat.values(layout, source.read("arr\\images.dat")[1], "grp_file")
    relations = anim.relations(storage.read("SD/images.rel"))
    if len(relations) != len(grp_files):
        raise HdError(f"images.rel has {len(relations)} entries, images.dat {len(grp_files)}")
    out: dict[int, int] = {}
    for image, ref in enumerate(grp_files):
        entry = gs.grp_of_tbl.get(ref)
        if entry is None or entry in out:
            continue
        owner = image if storage.has(f"{prefix}anim/main_{image:03d}.anim") else relations[image][1]
        if owner >= 0 and storage.has(f"{prefix}anim/main_{owner:03d}.anim"):
            out[entry] = owner
    return out


def pack_container(gs: kpd.GrpSet, pack: str, images: dict[int, anim.Image], owner_of: dict[int, int], scale: int,
                   page_size: int, profile_id: str, source_hash: bytes) -> tuple[Container, int, int]:
    """The HD pack of SD pack `pack`; (container, frames, pages)."""
    position = {id(e): i for i, e in enumerate(gs.grps)}
    entries = [(position[id(e)], e) for _, es in pack_entries(gs, pack) for e in es]
    owners = sorted({owner_of[i] for i, _ in entries if i in owner_of})
    sheets = [images[o].layers["diffuse"] for o in owners]
    placed, pages = shelves([(s.width, s.height) for s in sheets], page_size)
    at = dict(zip(owners, placed))
    frames = bytearray()
    count = 0
    for i, e in entries:
        image = images.get(owner_of.get(i, -1))
        if image is not None and len(image.frames) != len(e.grp.frames):
            raise HdError(f"{e.path}: {len(image.frames)} HD frames, the GRP has {len(e.grp.frames)}")
        for k, g in enumerate(e.grp.frames):
            count += 1
            if image is None or not g.width or not image.frames[k].width:
                frames += FRAME.pack(0, 0, 0, 0, 0, 0, 0)
                continue
            f = image.frames[k]
            page, x, y = at[owner_of[i]]
            dx2 = 2 * f.left + f.width - image.box[0] - scale * (2 * g.x + g.width - e.grp.width)
            dy2 = 2 * f.top + f.height - image.box[1] - scale * (2 * g.y + g.height - e.grp.height)
            frames += FRAME.pack(page, x + f.x, y + f.y, f.width, f.height, dx2, dy2)
    diffuse = [bytearray(page_size * page_size) for _ in range(pages)]  # BC3: a byte a pixel
    team = [bytearray(page_size * page_size // 2) for _ in range(pages)]  # BC1: half a byte
    for owner in owners:
        page, x, y = at[owner]
        image = images[owner]
        sheet = image.layers["diffuse"]
        fourcc, data = blocks(sheet.dds)
        if fourcc != b"DXT5":
            raise HdError(f"image {owner}: diffuse in {fourcc!r}, BC3 expected")
        blit_blocks(diffuse[page], page_size, x, y, sheet.width, sheet.height, data, DIFFUSE_BLOCK)
        mask = image.layers.get("teamcolor")
        if mask is not None:
            fourcc, data = blocks(mask.dds)
            if fourcc != b"DXT1" or (mask.width, mask.height) != (sheet.width, sheet.height):
                raise HdError(f"image {owner}: team colour mask {mask.width} x {mask.height} {fourcc!r}")
            blit_blocks(team[page], page_size, x, y, mask.width, mask.height, data, TEAM_BLOCK)
    c = Container(PACK_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<BHHI", scale, page_size, pages, count))
    c.add("FRMS", bytes(frames))
    c.add("DIFF", b"".join(diffuse))
    c.add("TEAM", b"".join(team))
    return c, count, pages


def generate(source: Source, storage: CascStorage, out: Path, variant: str) -> dict:
    """Write generated/hd/<variant>/; return counts."""
    prefix, scale = VARIANTS[variant]
    root = out / "hd" / variant
    profile_id, source_hash = source.profile.id, source.data_set_hash()
    gs = kpd.grp_set(*kpd.grp_references(source.read("arr\\images.dat")[1], source.read("arr\\images.tbl")[1]),
                     lambda p: source.read(p)[1])
    owner_of = hd_images(source, gs, storage, prefix)
    images: dict[int, anim.Image] = {}
    for owner in sorted(set(owner_of.values())):
        image = anim.parse(storage.read(f"{prefix}anim/main_{owner:03d}.anim"))
        if image.scale != scale:
            raise HdError(f"image {owner}: scale {image.scale}, {variant} has {scale}")
        images[owner] = image
    packs = {}
    for pack in dict.fromkeys(e.pack for e in gs.grps):
        c, frames, pages = pack_container(gs, pack, images, owner_of, scale, PAGE_SIZE[variant], profile_id, source_hash)
        write_if_changed(root / "packs" / f"{pack}.kpd", c.to_bytes())
        packs[pack] = (frames, pages)
    tiles = {name: write_tileset(source, storage, root, prefix, name, profile_id, source_hash) for name in TILESETS}
    log.info("hd %s: %d of %d GRPs drawn by %d anims; packs (frames, pages) %s; tiles %s", variant, len(owner_of),
             len(gs.grps), len(images), packs, tiles)
    return {"grps": len(owner_of), "images": len(images), "packs": packs, "tiles": tiles}


def write_tileset(source: Source, storage: CascStorage, root: Path, prefix: str, name: str, profile_id: str,
                  source_hash: bytes) -> int:
    megatiles = len(source.read(f"tileset\\{name}.vf4")[1]) // MEGATILE_VF4
    images = dds.container(storage.read(f"{prefix}TileSet/{TILESET_FILES[name]}.dds.vr4"))
    if len(images) < megatiles:
        raise HdError(f"{name}: {len(images)} HD tiles for {megatiles} megatiles")
    size = images[0][0]
    out = bytearray()
    fourcc = b""
    for i in range(megatiles):
        w, h, data = images[i]
        if (w, h) != (size, size):
            raise HdError(f"{name}: tile {i} is {w} x {h}, not {size} x {size}")
        fourcc, body = blocks(data)
        out += body
    c = Container(TILESET_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<HH4s", megatiles, size, fourcc))
    c.add("TILE", bytes(out))
    write_if_changed(root / "tilesets" / f"{name}.kpd", c.to_bytes())
    return megatiles
