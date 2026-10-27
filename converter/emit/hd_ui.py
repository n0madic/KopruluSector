"""StarCraft: Remastered's HD interface art (ADR 0013, stage R4d): `generated/hd/<variant>/ui.kpd` (`HDUI`).

The interface's files (ui.kpd's dialog GRPs, PCX and glue files) that have HD art in the user's install, frame for
frame: a GRP's `*.dds.grp` (the same frame count; an HD frame is the SD frame's trimmed rectangle at the variant's
scale, centred on it), a PCX's `.DDS` (the whole image, centred on it: a glue background is 16:9 and reaches into the
margins), a console's three pieces (`console_left`, `_center`, `_right`: placed where they match the SD console best,
their offsets in the console PCX's pixels). Sections:
- HEAD: u8 scale, u16 assets, u32 frames;
- ASET: per asset a 64-byte NUL-padded SD path, u32 first frame, u16 frames, u16 flags (1: the console's pieces);
- FRMS: per frame u32 offset into IMGS, u32 size, u16 width, u16 height (HD pixels), u8 format (0 BC1, 1 BC3,
  2 PNG RGBA), u8 0, i16 x, y (a console piece's top-left in the console PCX's pixels; else 0);
- IMGS: the images, BC blocks (rows of blocks over the width and height rounded up to 4) or PNG.
"""
from __future__ import annotations

import logging
import struct
from pathlib import Path

from .. import CONVERTER_VERSION
from ..casc.storage import CascStorage
from ..formats import dds
from ..kpf import kpd
from ..kpf.container import Container
from ..raster import Image, encode_png
from .files import write_if_changed
from .hd import SCHEMA, HdError, blocks

log = logging.getLogger(__name__)

UI_KIND = b"HDUI"
PATH_SIZE = 64
ASSET = struct.Struct("<64sIHH")
FRAME = struct.Struct("<IIHHBBhh")
FORMAT_BC1, FORMAT_BC3, FORMAT_PNG = 0, 1, 2
FLAG_CONSOLE = 1
CONSOLES = {"zconsole": "zerg", "tconsole": "terran", "pconsole": "protoss"}
RACES = {"z": "zerg", "t": "terran", "p": "protoss"}
PIECES = ("left", "center", "right")
GROUP_COLUMNS = ((0, 138), (138, 408), (408, 640))  # kp_layout.c's console groups
WIDE = 853  # the screen width Remastered's console pieces are drawn for (16:9 at 480 rows)
SEARCH = 12  # logical pixels a piece's offset is looked for around where WIDE puts it
SCREEN_H = 480


def hd_path(path: str) -> str | None:
    """The HD art's path (without the variant's prefix) of an SD interface file, or None."""
    p = path.replace("\\", "/").lower()
    name = p.rsplit("/", 1)[-1]
    if p == "unit/cmdbtns/cmdicons.grp":
        return "unit/cmdicons/cmdicons.dds.grp"
    if p.startswith("unit/cmdbtns/") and name.endswith("cmdbtns.grp") and name[0] in RACES:
        return f"game/consoles/{RACES[name[0]]}/cmdbtns.dds.grp"
    if p.startswith("dlgs/") and name[:-4] in RACES.values():
        return f"game/consoles/{name[:-4]}/dlg.dds.grp"
    if p.endswith(".grp"):
        return p[:-4] + ".dds.grp"
    if p.endswith(".pcx"):
        return p[:-4] + ".dds"
    return None


def image_record(data: bytes) -> tuple[int, int, int, bytes]:
    """(width, height, format, bytes) of a DDS image."""
    h = dds.header(data)
    if h.fourcc in (b"DXT1", b"DXT5"):
        fourcc, body = blocks(data)
        return h.width, h.height, FORMAT_BC1 if fourcc == b"DXT1" else FORMAT_BC3, body
    image = dds.decode(data)
    return image.width, image.height, FORMAT_PNG, encode_png(Image("RGBA", image.width, image.height,
                                                                   bytearray(image.rgba)))


def logical(image: dds.Image, scale: int) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    """An RGBA image averaged down by scale: (width, height, texels)."""
    w, h = image.width // scale, image.height // scale
    out = []
    for y in range(h):
        for x in range(w):
            acc = [0, 0, 0, 0]
            for dy in range(scale):
                row = ((y * scale + dy) * image.width + x * scale) * 4
                for dx in range(scale):
                    for c in range(4):
                        acc[c] += image.rgba[row + 4 * dx + c]
            n = scale * scale
            out.append((acc[0] // n, acc[1] // n, acc[2] // n, acc[3] // n))
    return w, h, out


def place_piece(sd: dds.Image, piece: tuple[int, int, list], group: tuple[int, int], guess: tuple[int, int]
                ) -> tuple[int, int]:
    """The offset (x, y) of a console piece (logical texels) in the SD console where its colours over the group's
    columns match best (the mean absolute difference of the texels opaque in both), around `guess`: every second
    offset and texel first, then the neighbours of the best."""
    pw, ph, texels = piece

    def error(ox: int, oy: int, step: int) -> float:
        total = n = 0
        for y in range(max(0, oy), min(sd.height, oy + ph), step):
            for x in range(group[0], group[1], step):
                px, py = x - ox, y - oy
                if not (0 <= px < pw):
                    continue
                o = (y * sd.width + x) * 4
                t = texels[py * pw + px]
                if sd.rgba[o + 3] < 200 or t[3] < 200:
                    continue
                total += abs(sd.rgba[o] - t[0]) + abs(sd.rgba[o + 1] - t[1]) + abs(sd.rgba[o + 2] - t[2])
                n += 1
        return total / n if n > 200 else float("inf")

    coarse = min(((error(ox, oy, 2), ox, oy) for oy in range(guess[1] - SEARCH, guess[1] + SEARCH + 1, 2)
                  for ox in range(guess[0] - SEARCH, guess[0] + SEARCH + 1, 2)))
    best = min(((error(ox, oy, 1), ox, oy) for oy in range(coarse[2] - 1, coarse[2] + 2)
                for ox in range(coarse[1] - 1, coarse[1] + 2)))
    if best[0] == float("inf"):
        raise HdError("a console piece matches no part of the SD console")
    return best[1], best[2]


def console_frames(storage: CascStorage, prefix: str, scale: int, race: str) -> list[tuple[int, int, int, bytes, int, int]]:
    """The race's console pieces: (width, height, format, bytes, x, y) each, x and y in the console PCX's pixels."""
    sd = dds.decode(storage.read(f"SD/game/consoles/{race}/console.DDS"))
    top = SCREEN_H - sd.height  # the SD console's first row in the 640 x 480 PCX
    out = []
    for piece, group in zip(PIECES, GROUP_COLUMNS):
        data = storage.read(f"{prefix}game/consoles/{race}/console_{piece}.DDS")
        image = dds.decode(data)
        small = logical(image, scale)
        guess = {"left": (-((WIDE - 640) // 2), sd.height - small[1]), "center": (group[0], sd.height - small[1]),
                 "right": (640 + (WIDE - 640) // 2 - small[0], sd.height - small[1])}[piece]
        ox, oy = place_piece(sd, small, group, guess)
        w, h, fmt, body = image_record(data)
        out.append((w, h, fmt, body, ox, top + oy))
    return out


def sd_paths(read) -> list[str]:
    """The interface's GRPs and PCX in ui.kpd (its dialog GRPs, PCX and glue files), without repeats."""
    defs = kpd.ui_defs()
    paths: list[str] = []
    seen: set[str] = set()
    for path in ([f"glue\\{t}\\Dlg.grp" for t in defs["themes"]] + defs["dialog_grps"] + defs["grps"] + defs["pcx"]
                 + kpd.glue_file_paths(read, defs)):
        if path.lower() not in seen and path.lower().endswith((".grp", ".pcx")):
            seen.add(path.lower())
            paths.append(path)
    return paths


def generate(read, storage: CascStorage, root: Path, prefix: str, scale: int, profile_id: str,
             source_hash: bytes) -> dict:
    """ui.kpd of one variant; counts."""
    assets: list[tuple[str, list[tuple[int, int, int, bytes, int, int]], int]] = []
    for path in sd_paths(read):
        stem = path.replace("\\", "/").lower().rsplit("/", 1)[-1][:-4]
        if stem in CONSOLES:
            assets.append((path, console_frames(storage, prefix, scale, CONSOLES[stem]), FLAG_CONSOLE))
            continue
        hd = hd_path(path)
        if hd is None:
            continue
        full = prefix + hd  # the storage's names ignore case
        if not storage.has(full):
            continue
        if hd.endswith(".dds.grp"):
            sd = read(path)
            count = struct.unpack_from("<H", sd, 0)[0] & 0x7FFF
            frames = dds.container(storage.read(full))
            if len(frames) != count:
                log.warning("hd ui: %s has %d HD frames for %d SD ones: left in SD", full, len(frames), count)
                continue
            assets.append((path, [image_record(data) + (0, 0) for _, _, data in frames], 0))
        else:
            assets.append((path, [image_record(storage.read(full)) + (0, 0)], 0))
    images = bytearray()
    aset = bytearray()
    frms = bytearray()
    first = 0
    for path, frames, flags in assets:
        name = path.lower().encode("latin-1")
        if len(name) >= PATH_SIZE:
            raise HdError(f"{path}: path too long for ASET")
        aset += ASSET.pack(name, first, len(frames), flags)
        for w, h, fmt, body, x, y in frames:
            frms += FRAME.pack(len(images), len(body), w, h, fmt, 0, x, y)
            images += body
            images += bytes(-len(images) % 4)
        first += len(frames)
    c = Container(UI_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<BHI", scale, len(assets), first))
    c.add("ASET", bytes(aset))
    c.add("FRMS", bytes(frms))
    c.add("IMGS", bytes(images))
    write_if_changed(root / "ui.kpd", c.to_bytes())
    log.info("hd ui: %d assets, %d frames, %d bytes", len(assets), first, len(images))
    return {"assets": len(assets), "frames": first}
