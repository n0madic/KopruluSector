"""StarCraft: Remastered's HD art for the HD renderer (ADR 0013, stage R2): `generated/hd/<variant>/`.

`hd2` (×2) and `hd` (×4) are written alike from the user's install, never shipped:
- `packs/<pack>.kpd` (`HDPK`), one per SD pack (`generated/packs/<pack>.kpd`), its frames in the SD pack's FRMS
  order, so an SD frame's index is its HD frame's: HEAD (u8 scale, u16 page size, u16 pages, u32 frames, u8 format
  of the pages: 0 BC), FRMS (per
  frame u16 page, x, y, width, height of the HD frame in its page, i16 dx, dy: its centre less the SD frame's centre
  times the scale, in half HD pixels, y down; width 0: no HD frame), DIFF (the pages' BC3 blocks: the diffuse
  sheets), TEAM (their BC1 blocks: the team colour masks, zero where an image has none). An image's frames come from
  its own anim (`anim/main_<image>.anim`) or the one `SD/images.rel` names; a GRP takes the first image naming it.
  Each frame keeps its blocks (its rectangle widened to whole blocks), packed with its image's other frames into
  2048 x 2048 pages, the unit the runtime streams;
- `tilesets/<name>.kpd` (`HDTS`): HEAD (u16 tile count: the tileset's 1.17 megatiles, u16 tile size, u32 FourCC),
  TILE (each megatile's BC1 blocks, tile i is megatile i of `.vf4`/`.vx4`: Remastered's appended tiles left out);
  where the tileset has water (`TileSet/<name>.tmsk`: megatile and frame of `<name>_mask.dds.grp`), WMAP (u16 per
  megatile: 0 dry, else 1 + its mask) and WMSK (the masks, tile size squared bytes each: the frame's red, 255 water;
  the empty and the repeated ones left out);
- `ui.kpd` (`HDUI`): the interface's HD art (converter/emit/hd_ui.py);
- `water.kpd` (`HDWA`): the water's normal maps (`effect/water_normal_1.dds.grp`, the large waves, and `_2`, the
  detail), animated frames of BC1 blocks: HEAD (u16 frames, u16 size of each set, FourCC), NRMA, NRMB.

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
PAGE_SIZE = {"hd2": 2048, "hd": 2048}  # the unit the runtime streams (kp_hd_cache); a frame is at most 1060 x 1024
OPEN_PAGES = 4  # the pages a frame may still go into
PACK_KIND, TILESET_KIND, WATER_KIND = b"HDPK", b"HDTS", b"HDWA"
FORMAT_BC = 0  # HEAD's page format: BC3 diffuse and BC1 team blocks (1: ETC2 RGBA8 and EAC R11, tools/product.sh)
SCHEMA = 2
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


class Skyline:
    """Bottom-left skyline packing of rectangles into one square page (in block units)."""

    def __init__(self, size: int):
        self.size = size
        self.line = [(0, 0, size)]  # (x, y, width) segments along the top edge of what is placed

    def find(self, w: int, h: int) -> tuple[int, int, int] | None:
        """(y, x, segment) of the lowest, then leftmost, place for w x h, or None."""
        best = None
        for i, (x, _, _) in enumerate(self.line):
            if x + w > self.size:
                break
            y, left, j = 0, w, i
            while left > 0:
                y = max(y, self.line[j][1])
                left -= self.line[j][2]
                j += 1
            if y + h <= self.size and (best is None or (y, x) < best[:2]):
                best = (y, x, i)
        return best

    def place(self, w: int, h: int, at: tuple[int, int, int]) -> None:
        y, x, i = at
        out = self.line[:i] + [(x, y + h, w)]
        end = x + w
        for sx, sy, sw in self.line[i:]:
            if sx + sw > end:
                start = max(sx, end)
                out.append((start, sy, sx + sw - start))
        merged: list[tuple[int, int, int]] = []
        for seg in out:
            if merged and merged[-1][1] == seg[1]:
                merged[-1] = (merged[-1][0], seg[1], merged[-1][2] + seg[2])
            else:
                merged.append(seg)
        self.line = merged


def pack_groups(groups: list[list[tuple[int, int]]], page: int, open_pages: int = OPEN_PAGES
                ) -> tuple[list[list[tuple[int, int, int]]], int]:
    """(page, x, y) of each w x h rectangle (multiples of 4) of each group, and the page count. A group's rectangles
    (an image's frames), tallest first, go into the last `open_pages` pages, lowest place first, else a new page: an
    image's frames stay on few pages, which are streamed (kp_hd_cache)."""
    pages: list[Skyline] = []
    out: list[list[tuple[int, int, int]]] = []
    for sizes in groups:
        placed: list[tuple[int, int, int]] = [(0, 0, 0)] * len(sizes)
        for i in sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0], i)):
            w, h = sizes[i][0] // BLOCK, sizes[i][1] // BLOCK
            if w > page // BLOCK or h > page // BLOCK:
                raise HdError(f"a {sizes[i][0]} x {sizes[i][1]} frame does not fit a {page} page")
            best = None
            for k in range(max(0, len(pages) - open_pages), len(pages)):
                at = pages[k].find(w, h)
                if at is not None and (best is None or at[:2] < best[1][:2]):
                    best = (k, at)
            if best is None:
                pages.append(Skyline(page // BLOCK))
                best = (len(pages) - 1, pages[-1].find(w, h))
            k, at = best
            pages[k].place(w, h, at)
            placed[i] = (k, at[1] * BLOCK, at[0] * BLOCK)
        out.append(placed)
    return out, len(pages)


def block_rect(f: anim.Frame) -> tuple[int, int, int, int]:
    """The frame's rectangle in its sheet widened to whole blocks: (x, y, w, h)."""
    x, y = f.x // BLOCK * BLOCK, f.y // BLOCK * BLOCK
    return x, y, -(-(f.x + f.width) // BLOCK) * BLOCK - x, -(-(f.y + f.height) // BLOCK) * BLOCK - y


def blit_blocks(page: bytearray, page_size: int, x: int, y: int, w: int, h: int, data: bytes, step: int,
                sheet_w: int | None = None, sx: int = 0, sy: int = 0) -> None:
    """The w x h blocks at (sx, sy) of a sheet sheet_w wide (default w: all of it) into the page at (x, y)."""
    columns, row = page_size // BLOCK, (w // BLOCK) * step
    stride = ((sheet_w if sheet_w is not None else w) + BLOCK - 1) // BLOCK * step
    for by in range(h // BLOCK):
        to = ((y // BLOCK + by) * columns + x // BLOCK) * step
        at = (sy // BLOCK + by) * stride + sx // BLOCK * step
        page[to:to + row] = data[at:at + row]


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
    """The HD pack of SD pack `pack`; (container, frames, pages). Each image's frames are packed by their block
    rectangles (a rectangle two frames share once), the image's frames together."""
    position = {id(e): i for i, e in enumerate(gs.grps)}
    entries = [(position[id(e)], e) for _, es in pack_entries(gs, pack) for e in es]
    owners = sorted({owner_of[i] for i, _ in entries if i in owner_of})
    rects = {o: list(dict.fromkeys(block_rect(f) for f in images[o].frames if f.width)) for o in owners}
    placed, pages = pack_groups([[(w, h) for _, _, w, h in rects[o]] for o in owners], page_size)
    at = {(o, r): p for o, ps in zip(owners, placed) for r, p in zip(rects[o], ps)}
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
            r = block_rect(f)
            page, x, y = at[(owner_of[i], r)]
            dx2 = 2 * f.left + f.width - image.box[0] - scale * (2 * g.x + g.width - e.grp.width)
            dy2 = 2 * f.top + f.height - image.box[1] - scale * (2 * g.y + g.height - e.grp.height)
            frames += FRAME.pack(page, x + f.x - r[0], y + f.y - r[1], f.width, f.height, dx2, dy2)
    diffuse = [bytearray(page_size * page_size) for _ in range(pages)]  # BC3: a byte a pixel
    team = [bytearray(page_size * page_size // 2) for _ in range(pages)]  # BC1: half a byte
    for owner in owners:
        image = images[owner]
        sheet = image.layers["diffuse"]
        fourcc, data = blocks(sheet.dds)
        if fourcc != b"DXT5":
            raise HdError(f"image {owner}: diffuse in {fourcc!r}, BC3 expected")
        mask = image.layers.get("teamcolor")
        mask_data = None
        if mask is not None:
            fourcc, mask_data = blocks(mask.dds)
            if fourcc != b"DXT1" or (mask.width, mask.height) != (sheet.width, sheet.height):
                raise HdError(f"image {owner}: team colour mask {mask.width} x {mask.height} {fourcc!r}")
        for r in rects[owner]:
            page, x, y = at[(owner, r)]
            blit_blocks(diffuse[page], page_size, x, y, r[2], r[3], data, DIFFUSE_BLOCK, sheet.width, r[0], r[1])
            if mask_data is not None:
                blit_blocks(team[page], page_size, x, y, r[2], r[3], mask_data, TEAM_BLOCK, sheet.width, r[0], r[1])
    c = Container(PACK_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<BHHIB", scale, page_size, pages, count, FORMAT_BC))
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
    normals = write_water(storage, root, prefix, profile_id, source_hash)
    from . import hd_ui
    ui = hd_ui.generate(lambda path: source.read(path)[1], storage, root, prefix, scale, profile_id, source_hash)
    log.info("hd %s: %d of %d GRPs drawn by %d anims; packs (frames, pages) %s; tiles %s; water normals %s; ui %s",
             variant, len(owner_of), len(gs.grps), len(images), packs, tiles, normals, ui)
    return {"grps": len(owner_of), "images": len(images), "packs": packs, "tiles": tiles, "normals": normals,
            "ui": ui}


def bc1_red(body: bytes, w: int, h: int) -> bytes:
    """The red channel (one byte a texel, rows top first) of w x h BC1 blocks."""
    out = bytearray(w * h)
    columns = w // BLOCK
    for b in range(len(body) // TEAM_BLOCK):
        c0, c1, bits = struct.unpack_from("<HHI", body, b * TEAM_BLOCK)
        r0, r1 = (c0 >> 11) * 255 // 31, (c1 >> 11) * 255 // 31
        reds = (r0, r1, (2 * r0 + r1) // 3, (r0 + 2 * r1) // 3) if c0 > c1 else (r0, r1, (r0 + r1) // 2, 0)
        x0, y0 = (b % columns) * BLOCK, (b // columns) * BLOCK
        for i in range(16):
            out[(y0 + i // 4) * w + x0 + i % 4] = reds[bits >> (2 * i) & 3]
    return bytes(out)


def water(storage: CascStorage, prefix: str, name: str, megatiles: int, size: int) -> tuple[bytes, bytes] | None:
    """(WMAP, WMSK) of a tileset with water, or None."""
    file = TILESET_FILES[name]
    if not storage.has(f"{prefix}TileSet/{file}.tmsk"):
        return None
    tmsk = storage.read(f"{prefix}TileSet/{file}.tmsk")
    if tmsk[:4] != b"KSMT":
        raise HdError(f"{name}.tmsk: not a KSMT file")
    _, count = struct.unpack_from("<HH", tmsk, 4)
    frames = dds.container(storage.read(f"{prefix}TileSet/{file}_mask.dds.grp"))
    wmap = [0] * megatiles
    masks: dict[bytes, int] = {}
    for k in range(count):
        megatile, frame = struct.unpack_from("<HH", tmsk, 8 + 4 * k)
        if megatile >= megatiles:
            continue
        w, h, data = frames[frame]
        if (w, h) != (size, size):
            raise HdError(f"{name}: water mask {frame} is {w} x {h}, the tiles {size} x {size}")
        fourcc, body = blocks(data)
        if fourcc != b"DXT1":
            raise HdError(f"{name}: water mask {frame} in {fourcc!r}")
        red = bc1_red(body, size, size)
        if any(red):
            wmap[megatile] = 1 + masks.setdefault(red, len(masks))
    return struct.pack(f"<{megatiles}H", *wmap), b"".join(masks)


def write_water(storage: CascStorage, root: Path, prefix: str, profile_id: str, source_hash: bytes) -> tuple[int, int]:
    """water.kpd; (large frames, detail frames)."""
    sets = []
    for k in (1, 2):
        frames = dds.container(storage.read(f"{prefix}effect/water_normal_{k}.dds.grp"))
        size = frames[0][0]
        body = bytearray()
        for w, h, data in frames:
            fourcc, b = blocks(data)
            if (w, h) != (size, size) or fourcc != b"DXT1":
                raise HdError(f"water_normal_{k}: a frame {w} x {h} {fourcc!r}")
            body += b
        sets.append((len(frames), size, bytes(body)))
    c = Container(WATER_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<HHHH4s", sets[0][0], sets[0][1], sets[1][0], sets[1][1], b"DXT1"))
    c.add("NRMA", sets[0][2])
    c.add("NRMB", sets[1][2])
    write_if_changed(root / "water.kpd", c.to_bytes())
    return sets[0][0], sets[1][0]


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
    wet = water(storage, prefix, name, megatiles, size)
    if wet is not None:
        c.add("WMAP", wet[0])
        c.add("WMSK", wet[1])
    write_if_changed(root / "tilesets" / f"{name}.kpd", c.to_bytes())
    return megatiles
