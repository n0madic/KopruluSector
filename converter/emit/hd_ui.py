"""StarCraft: Remastered's HD interface art (ADR 0013, stage R4d): `generated/hd/<variant>/ui.kpd` (`HDUI`).

The interface's files (ui.kpd's dialog GRPs, PCX and glue files) that have HD art in the user's install, frame for
frame: a GRP's `*.dds.grp` (the same frame count; an HD frame lies where its opaque texels agree best with the SD
frame's, `place_frame`: most are the SD frame's trimmed rectangle at the variant's scale, centred on it, the minimap's
blink keeps its GRP canvas from the origin), a PCX's `.DDS` (the whole image, centred on it: a glue background is 16:9
and reaches into the margins), a console's three pieces (`console_left`, `_center`, `_right`: on the screen's bottom,
the outer ones at a 16:9 screen's edges, the centre one where its edges match the SD console's). Every frame records
where it lies, so the runtime places them all alike. A
PCX's HD art is taken only when it is the SD picture drawn again (`same_picture`): Remastered drew most glue screens
anew for its own layout (their frames would not lie under the SD dialogs), and those stay SD. Sections:
- HEAD: u8 scale, u16 assets, u32 frames;
- ASET: per asset a 64-byte NUL-padded SD path, u32 first frame, u16 frames, u16 flags (1: the console's pieces);
- FRMS: per frame u32 offset into IMGS, u32 size, u16 width, u16 height (HD pixels), u8 format (0 BC1, 1 BC3,
  2 PNG RGBA), u8 0, i16 x, y (the HD image's top-left in HD pixels from the top-left of the SD frame it replaces: a
  GRP frame's, a PCX's, the console PCX's);
- IMGS: the images, BC blocks (rows of blocks over the width and height rounded up to 4) or PNG.
"""
from __future__ import annotations

import logging
import struct
from pathlib import Path

from .. import CONVERTER_VERSION
from ..casc import rebuild
from ..casc.storage import CascStorage
from ..formats import dds, grp, pcx
from ..kpf import kpd
from ..kpf.container import Container
from ..raster import Image, encode_png
from .files import write_if_changed
from .hd import HdError, blocks

log = logging.getLogger(__name__)

UI_KIND = b"HDUI"
UI_SCHEMA = 3  # every frame's x, y: where it lies (kp_present.h KP_HD_UI_SCHEMA)
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
WIRE_LAYERS = 8  # a wireframe's HD frame: its parts' layers side by side, square (kp_present.h KP_HD_UI_WIRE_LAYERS)
PLACE_MARGIN = 0.3  # how much better than its usual place another must agree to take it (measured on HD2: the frames
# the usual place misses gain 0.33-1.0, those drawn anew at most 0.18)
PLACE_EXACT = 0.95  # an agreement that takes another place outright (the frame drawn again there)
OPAQUE_BITS = bytes(ord("1") if a >= 128 else ord("0") for a in range(256))  # an alpha byte as a mask bit (half on)


def console_picture(path: str) -> bool:
    """A race's progress bar (`game\\tpbrempt.pcx`, `tpbrfull.pcx`): Remastered keeps its HD art with the race's
    console (`game/consoles/terran/pbrempt.dds`), the same picture with rounded ends (HD's gradients then correlate as
    little as -0.18 with the SD frame's, so the picture check does not decide it)."""
    p = path.replace("\\", "/").lower()
    name = p.rsplit("/", 1)[-1]
    return p.startswith("game/") and name[0] in RACES and name[1:] in ("pbrempt.pcx", "pbrfull.pcx")


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
    if console_picture(path):
        return f"game/consoles/{RACES[name[0]]}/{name[1:-4]}.dds"
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


def sd_frames(data: bytes) -> list[tuple[int, int, int, int, bytes]]:
    """A GRP's frames: (x, y, width, height, mask: 1 opaque, rows top first); a dialog GRP's plain rows are opaque where
    not index 0 (its frames are blitted keyed)."""
    layout = rebuild.uncompressed_grp_layout(data)
    if layout is None:
        return [(f.x, f.y, f.width, f.height, bytes(1 if m else 0 for m in f.mask)) for f in grp.decode(data).frames]
    return [(x, y, w, h, bytes(1 if v else 0 for v in data[off:off + w * h])) for x, y, w, h, off in layout["frames"]]


def mask_rows(mask: bytes, w: int, h: int, scale: int) -> list[int]:
    """A mask's rows at scale (bit x of a row: column x opaque)."""
    out = []
    for y in range(h):
        bits = int("0" + "".join("1" * scale if m else "0" * scale for m in reversed(mask[y * w:(y + 1) * w])), 2)
        out += [bits] * scale
    return out


def alpha_rows(image: tuple[int, int, bytes], layer_w: int) -> list[int]:
    """An HD image's (width, height, alpha) rows of opaque texels (bit x: column x), its layers of layer_w columns ORed
    (a wireframe's)."""
    w, h, alpha = image
    out = []
    for y in range(h):
        row = alpha[y * w:(y + 1) * w].translate(OPAQUE_BITS)
        bits = 0
        for x in range(0, w, max(layer_w, 1)):
            bits |= int(b"0" + row[x:x + layer_w][::-1], 2)
        out.append(bits)
    return out


def centred(sd_w: int, sd_h: int, hd_w: int, hd_h: int, scale: int) -> tuple[int, int]:
    """The top-left in HD pixels, from an SD rectangle's, of an HD image centred on it (rounded up and left)."""
    return (scale * sd_w - hd_w) // 2, (scale * sd_h - hd_h) // 2


def overlap(a: list[int], b: list[int], dx: int, dy: int) -> int:
    """The opaque texels rows a and b share, b moved by (dx, dy) (a's columns and rows from 0)."""
    total = 0
    for y, bits in enumerate(b):
        if 0 <= y + dy < len(a):
            total += (a[y + dy] & (bits << dx if dx >= 0 else bits >> -dx)).bit_count()
    return total


def place_frame(frame: tuple[int, int, int, int, bytes], canvas: tuple[int, int], image: tuple[int, int, bytes],
                scale: int) -> tuple[int, int, bool]:
    """Where an HD frame (width, height, alpha) lies on the SD frame it replaces: its top-left in HD pixels from the SD frame's, and whether
    that is not its usual place. Remastered keeps most frames trimmed and centred on the SD frame and a wireframe
    centred on the GRP canvas; some lie elsewhere (the minimap's blink from the canvas's origin, a transparent margin
    before the content; the placeholder wireframes' text): another of those places is taken where the frames' opaque
    texels agree (intersection over union) by PLACE_MARGIN better there, or PLACE_EXACT."""
    x, y, w, h, mask = frame
    hd_w, hd_h = image[0], image[1]
    wire = hd_h > 0 and hd_w == WIRE_LAYERS * hd_h
    side = hd_h if wire else hd_w
    a, b = mask_rows(mask, w, h, scale), alpha_rows(image, side)
    na, nb = sum(r.bit_count() for r in a), sum(r.bit_count() for r in b)

    def agreement(at: tuple[int, int]) -> float:
        shared = overlap(a, b, *at)
        return shared / (na + nb - shared) if na + nb > shared else 0.0

    on_frame = centred(w, h, side, hd_h, scale)
    on_canvas = centred(canvas[0] - 2 * x, canvas[1] - 2 * y, side, hd_h, scale)
    usual = on_canvas if wire else on_frame
    other = max((p for p in (on_frame, (-scale * x, -scale * y), on_canvas) if p != usual), key=agreement,
                default=usual)
    there, here = agreement(other), agreement(usual)
    if other != usual and (there >= here + PLACE_MARGIN or there >= PLACE_EXACT > here):
        return other[0], other[1], True
    return usual[0], usual[1], False


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
    """The race's console pieces: (width, height, format, bytes, x, y) each, x and y in HD pixels from the console PCX's
    top-left. The pieces stand on the screen's bottom; the left and right ones at the edges of a WIDE screen around the
    640 columns, the centre one where its edges match the SD console's status window."""
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
        out.append((w, h, fmt, body, ox * scale, (top + oy) * scale))
    return out


def same_picture(sd: pcx.Pcx, hd: dds.Image, scale: int) -> float:
    """The correlation of the luminance gradients of an SD picture and its HD art centred on it (every other logical
    pixel, the HD art sampled at it)."""
    ox, oy = centred(sd.width, sd.height, hd.width, hd.height, scale)

    def sd_luma(x: int, y: int) -> float:
        r, g, b = sd.palette[sd.pixels[y * sd.width + x]]
        return 0.3 * r + 0.59 * g + 0.11 * b

    def hd_luma(x: int, y: int) -> float | None:
        hx, hy = x * scale - ox, y * scale - oy
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
    moved = []
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
            try:
                sd_frame = sd_frames(sd)
            except grp.GrpError as e:
                log.warning("hd ui: %s: %s: left in SD", path, e)
                continue
            canvas = struct.unpack_from("<HH", sd, 2)
            frames = dds.container(storage.read(full))
            if len(frames) != len(sd_frame):
                log.warning("hd ui: %s has %d HD frames for %d SD ones: left in SD", full, len(frames), len(sd_frame))
                continue
            records = []
            for k, (_, _, data) in enumerate(frames):
                x, y, elsewhere = place_frame(sd_frame[k], canvas, dds.alpha(data), scale)
                if elsewhere:
                    moved.append(f"{path}:{k}")
                records.append(image_record(data) + (x, y))
            assets.append((path, records, 0))
        else:
            data = storage.read(full)
            sd = pcx.decode(read(path))
            hd = dds.decode(data)
            if sd.palette is None or (not console_picture(path) and same_picture(sd, hd, scale) < SAME_PICTURE):
                drawn_anew += 1
                continue
            assets.append((path, [image_record(data) + centred(sd.width, sd.height, hd.width, hd.height, scale)], 0))
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
    c = Container(UI_KIND, UI_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<BHI", scale, len(assets), first))
    c.add("ASET", bytes(aset))
    c.add("FRMS", bytes(frms))
    c.add("IMGS", bytes(images))
    write_if_changed(root / "ui.kpd", c.to_bytes())
    log.info("hd ui: %d assets, %d frames, %d bytes; %d pictures drawn anew left in SD", len(assets), first, len(images),
             drawn_anew)
    log.info("hd ui: %d GRP frames lie off their usual place: %s", len(moved), ", ".join(moved))
    return {"assets": len(assets), "frames": first, "drawn_anew": drawn_anew, "moved": len(moved)}
