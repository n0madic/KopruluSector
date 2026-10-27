"""StarCraft: Remastered's HD interface art (ADR 0013, stage R4d): `generated/hd/<variant>/ui.kpd` (`HDUI`).

The interface's files (ui.kpd's dialog GRPs, PCX and glue files) that have HD art in the user's install, frame for
frame: a GRP's `*.dds.grp` (the same frame count; an HD frame is the SD frame's trimmed rectangle at the variant's
scale, centred on it), a PCX's `.DDS` (the whole image, centred on it: a glue background is 16:9 and reaches into the
margins), a console's three pieces (`console_left`, `_center`, `_right`: on the screen's bottom, the outer ones at a
16:9 screen's edges, the centre one where its edges match the SD console's; their offsets in the console PCX's pixels). A
PCX's HD art is taken only when it is the SD picture drawn again (`same_picture`): Remastered drew most glue screens
anew for its own layout (their frames would not lie under the SD dialogs), and those stay SD. Sections:
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
from ..formats import dds, pcx
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
SAME_PICTURE = 0.5  # the least gradient correlation of a PCX's HD art with the SD picture (measured: the same pictures
# 0.76-1.0, those drawn anew -0.42-0.39)


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


def gradients(w: int, h: int, texel) -> dict[tuple[int, int], tuple[float, float]]:
    """The luminance gradient (right and down differences) at each texel whose neighbours are opaque."""
    luma = [[-1.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = texel(x, y)
            if a >= 200:
                luma[y][x] = 0.3 * r + 0.59 * g + 0.11 * b
    out = {}
    for y in range(h - 1):
        for x in range(w - 1):
            c, right, down = luma[y][x], luma[y][x + 1], luma[y + 1][x]
            if c >= 0 and right >= 0 and down >= 0:
                out[(x, y)] = (right - c, down - c)
    return out


def place_piece(sd: dds.Image, piece: tuple[int, int, list], group: tuple[int, int], guess: int, y: int) -> int:
    """The x offset of a console piece (logical texels, its top at y) in the SD console where its luminance gradients
    over the group's columns agree best with the SD console's (their mean dot product: the art was redrawn, its edges
    stayed), within SEARCH of `guess`."""
    pw, ph, texels = piece
    sd_grad = gradients(sd.width, sd.height,
                        lambda x, yy: tuple(sd.rgba[(yy * sd.width + x) * 4:(yy * sd.width + x) * 4 + 4]))
    piece_grad = [(x, yy, g) for (x, yy), g in gradients(pw, ph, lambda x, yy: texels[yy * pw + x]).items()]

    def score(ox: int) -> float:
        total = n = 0
        for x, yy, (gx, gy) in piece_grad:
            g = sd_grad.get((x + ox, yy + y))
            if g is None or not (group[0] <= x + ox < group[1]):
                continue
            total += gx * g[0] + gy * g[1]
            n += 1
        return total / n if n > 200 else float("-inf")

    best = max(range(guess - SEARCH, guess + SEARCH + 1), key=score)
    if score(best) == float("-inf"):
        raise HdError("a console piece matches no part of the SD console")
    return best


def console_frames(storage: CascStorage, prefix: str, scale: int, race: str) -> list[tuple[int, int, int, bytes, int, int]]:
    """The race's console pieces: (width, height, format, bytes, x, y) each, x and y in the console PCX's pixels. The
    pieces stand on the screen's bottom; the left and right ones at the edges of a WIDE screen around the 640 columns,
    the centre one where its edges match the SD console's status window."""
    sd = dds.decode(storage.read(f"SD/game/consoles/{race}/console.DDS"))
    top = SCREEN_H - sd.height  # the SD console's first row in the 640 x 480 PCX
    out = []
    for piece, group in zip(PIECES, GROUP_COLUMNS):
        data = storage.read(f"{prefix}game/consoles/{race}/console_{piece}.DDS")
        w, h, fmt, body = image_record(data)
        pw, ph = w // scale, h // scale
        oy = sd.height - ph
        if piece == "left":
            ox = -((WIDE - 640) // 2)
        elif piece == "right":
            ox = 640 + (WIDE - 640 + 1) // 2 - pw
        else:
            ox = place_piece(sd, logical(dds.decode(data), scale), group, group[0], oy)
        out.append((w, h, fmt, body, ox, top + oy))
    return out


def same_picture(sd: pcx.Pcx, hd: dds.Image, scale: int) -> float:
    """The correlation of the luminance gradients of an SD picture and its HD art centred on it (every other logical
    pixel, the HD art sampled at it)."""
    w, h = hd.width // scale, hd.height // scale
    ox, oy = (sd.width - w) // 2, (sd.height - h) // 2

    def sd_luma(x: int, y: int) -> float:
        r, g, b = sd.palette[sd.pixels[y * sd.width + x]]
        return 0.3 * r + 0.59 * g + 0.11 * b

    def hd_luma(x: int, y: int) -> float | None:
        hx, hy = (x - ox) * scale, (y - oy) * scale
        if not (0 <= hx < hd.width and 0 <= hy < hd.height):
            return None
        i = (hy * hd.width + hx) * 4
        return 0.3 * hd.rgba[i] + 0.59 * hd.rgba[i + 1] + 0.11 * hd.rgba[i + 2]

    num = sd_sum = hd_sum = 0.0
    for y in range(0, sd.height - 2, 2):
        for x in range(0, sd.width - 2, 2):
            h0, h1, h2 = hd_luma(x, y), hd_luma(x + 2, y), hd_luma(x, y + 2)
            if h0 is None or h1 is None or h2 is None:
                continue
            s0 = sd_luma(x, y)
            sx, sy, hx, hy = sd_luma(x + 2, y) - s0, sd_luma(x, y + 2) - s0, h1 - h0, h2 - h0
            num += sx * hx + sy * hy
            sd_sum += sx * sx + sy * sy
            hd_sum += hx * hx + hy * hy
    return num / (sd_sum * hd_sum) ** 0.5 if sd_sum > 0 and hd_sum > 0 else 0.0


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
    drawn_anew = 0
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
            data = storage.read(full)
            sd = pcx.decode(read(path))
            if sd.palette is None or same_picture(sd, dds.decode(data), scale) < SAME_PICTURE:
                drawn_anew += 1
                continue
            assets.append((path, [image_record(data) + (0, 0)], 0))
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
    log.info("hd ui: %d assets, %d frames, %d bytes; %d pictures drawn anew left in SD", len(assets), first, len(images),
             drawn_anew)
    return {"assets": len(assets), "frames": first, "drawn_anew": drawn_anew}
