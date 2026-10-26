#!/usr/bin/env python3
"""Rebuild the map viewer's composed views on the CPU and compare them with the GPU.

The viewer's HTML5 autotest (viewer/viewer.script, check_compose) prints for each composed
view (<mission>, <mission>@<ticks>, @fx<ticks>, @image<id>) the CRC-32 of the index target
(rows top-down), its composition plan and the draw list of the images meeting it:
  SPIKE compose <view> <tileset> <cam x> <cam y> <width> <height> <crc> <images>
  SPIKE draw <view> <rank> <method> <owner> <param> <grp path> <frame> <left> <top> <flip> <row> ...
  SPIKE bar <view> <rank> <width> <height> <pixels: two hex digits each, "--" where none>
This script draws the same view with the original's kernels from the retail files
(docs/research/native-pixel-blitters.md, native-pixel-effects.md), independently of the
engine: terrain megatiles from the tileset, then the images in rank order:
  methods 0/1: owner colour map (tunit row on indices 8..15) over the source index;
  13: tselect row `param` on indices 1..8; 14: the colour map of the player in the drawing
     parameter; 16: tileset shift.pcx;
  9: blend bank of the images.dat remapping (allocation row = source index, identity rows
     past the allocation) by destination; 6: the trans50 bank; 10: dark.pcx row 18 by
     destination; 17: ofire allocation row = parameter low byte;
  3, 8 (and 2/4 before their threshold): in place, each pixel copies the one brightness
     (raw index for 8) bytes ahead, behind when mirrored, wrapping at the buffer end, in the
     original's traversal order; 2, 4, 5, 7: then the owner-mapped index where its
     brightness exceeds the parameter byte (signed); 5/7 blend through trans50 first;
  12: the warp texture frame `param` at the frame's pixel positions; 15: the frame edges in
     the logged colour; 11: the logged bar pixels (native/kp_present/kp_health_bar.c, checked
     against the original by tests/c/test_present.c health_bars_equal_original).
It reports, per view, whether the CRCs match.

Usage: python3 tools/viewer_compose_check.py --data RETAIL_DIR [--log build/spikes/viewer/web.log]
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from converter.derive.terrain import import_terrain, megatile_flags  # noqa: E402
from converter.formats import grp, pcx, tileset  # noqa: E402
from converter.formats.palette import TILESETS  # noqa: E402
from converter.kpf import container  # noqa: E402
from converter.mpq.profile import Profile, Resolver  # noqa: E402

log = logging.getLogger("viewer_compose_check")

REMAP_TABLES = ("ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green")  # images.dat remapping 1..7
SHADOW_ROW = 18
PLAYERS = 12
BANK_ROWS = 256  # the viewer pads every blend bank with identity rows to one row per byte value
PLAYER_COLORS = 8  # tunit entries per player, mapped onto indices 8..15
COLOR_FIRST = 8
TRANS50 = 4  # bank of the cloak blends 5-7 (DAT_00508890, images.dat remapping 5)
IDENTITY = bytes(range(256))
COLOR_SPAN, PLAYER_ROW_FIRST = 65536, 1  # viewer row codes: colour row * 65536 (5/7); owner p -> row p + 1
SELECTION_FIRST, SELECTION_COLORS = 1, 8  # tselect row k replaces indices 1..8 (0x40e900)
BRIGHT_WEIGHTS, BRIGHT_ROUND, BRIGHT_SHIFT = (77, 151, 28), 4096, 13  # 0x445f30
WARP_TEXTURE = "unit\\protoss\\texture.grp"
HEALTH_BAR = 11
MEGATILE = 32
TILE_MASK = 0x7FFF  # megatile index bits of a map cell


class CheckError(RuntimeError):
    """The log or the data cannot be checked."""


def parse_log(path: Path) -> dict[str, dict]:
    views: dict[str, dict] = {}
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[0] != "SPIKE":
            continue
        if parts[1] == "compose":
            name = parts[2]
            tileset_index, cam_x, cam_y, w, h, crc, count = map(int, parts[3:10])
            views[name] = {"tileset": tileset_index, "cam": (cam_x, cam_y), "size": (w, h), "crc": crc,
                           "count": count, "draws": []}
        elif parts[1] == "draw":
            name = parts[2]
            rank, method, owner, param = map(int, parts[3:7])
            path_, frame, left, top, flip = parts[7], int(parts[8]), int(parts[9]), int(parts[10]), int(parts[11])
            row = int(parts[12]) if len(parts) > 12 else 0
            views[name]["draws"].append((rank, method, owner, param, path_, frame, left, top, flip, row))
        elif parts[1] == "plan":
            # plan <view> <windows> <layers> <images past the capacity> <windows>
            views[parts[2]]["overflow"] = int(parts[5])
        elif parts[1] == "pixels":
            views[parts[2]].setdefault("pixels", {})[int(parts[3])] = bytes.fromhex(parts[4])
        elif parts[1] == "bar":
            rank, w, h, hexes = int(parts[3]), int(parts[4]), int(parts[5]), parts[6]
            cells = [None if hexes[k:k + 2] == "--" else int(hexes[k:k + 2], 16) for k in range(0, len(hexes), 2)]
            if len(cells) != w * h:
                raise CheckError(f"{parts[2]}: bar of rank {rank} has {len(cells)} pixels, not {w}x{h}")
            views[parts[2]].setdefault("bars", {})[rank] = (w, h, cells)
    return views


class Data:
    def __init__(self, resolver: Resolver) -> None:
        self.r = resolver
        self.grps: dict[str, grp.Grp] = {}
        self.tilesets: dict[int, tuple] = {}
        tunit = pcx.decode(resolver.read("game\\tunit.pcx")[1]).pixels
        self.player_maps = []
        for p in range(PLAYERS):
            m = bytearray(range(256))
            m[COLOR_FIRST:COLOR_FIRST + PLAYER_COLORS] = tunit[p * PLAYER_COLORS:(p + 1) * PLAYER_COLORS]
            self.player_maps.append(bytes(m))
        tselect = pcx.decode(resolver.read("game\\tselect.pcx")[1]).pixels
        self.selection_maps = []
        for k in range(len(tselect) // SELECTION_COLORS):
            m = bytearray(range(256))
            m[SELECTION_FIRST:SELECTION_FIRST + SELECTION_COLORS] = tselect[k * SELECTION_COLORS:(k + 1) * SELECTION_COLORS]
            self.selection_maps.append(bytes(m))

    def grp(self, path: str) -> grp.Grp:
        if path not in self.grps:
            self.grps[path] = grp.decode(self.r.read(path)[1])
        return self.grps[path]

    def tileset(self, index: int) -> tuple:
        if index not in self.tilesets:
            name = TILESETS[index]
            raw = {ext: self.r.read(f"tileset\\{name}.{ext}")[1] for ext in ("cv5", "vx4", "vr4", "vf4")}
            ts = tileset.decode(raw["cv5"], raw["vx4"], raw["vr4"], raw["vf4"])

            def table(t: str) -> bytes:
                return pcx.decode(self.r.read(f"tileset\\{name}\\{t}.pcx")[1]).pixels

            # Native blend allocation: (height + 1) rows, the decoded PCX after a leading row
            # (zero here, as in palettes.kpd; the original leaves it uninitialised). Past it the
            # original reads unrelated memory; like the viewer, identity rows keep the destination.
            identity = bytes(range(256))
            banks = []
            for t in REMAP_TABLES:
                bank = bytes(256) + table(t)
                banks.append(bank + identity * (BANK_ROWS - len(bank) // 256))
            wpe = self.r.read(f"tileset\\{name}.wpe")[1]
            bright = bytes((BRIGHT_WEIGHTS[0] * wpe[i * 4] + BRIGHT_WEIGHTS[1] * wpe[i * 4 + 1]
                            + BRIGHT_WEIGHTS[2] * wpe[i * 4 + 2] + BRIGHT_ROUND) >> BRIGHT_SHIFT for i in range(256))
            self.tilesets[index] = (ts, raw, banks, table("dark"), table("shift")[:256], bright)
        return self.tilesets[index]


def signed_byte(v: int) -> int:
    v &= 0xFF
    return v - 256 if v >= 128 else v


def frame_pixels(f: grp.Frame, x0: int, y0: int, w: int, h: int, flip: bool):
    """(destination offset, source index) of the opaque frame pixels inside the view, in the
    original's traversal order: rows top-down, screen x increasing (normal) or decreasing
    (mirrored, the right-to-left destination walk of the mirrored kernels)."""
    for yy in range(f.height):
        y = y0 + yy
        if not 0 <= y < h:
            continue
        cols = range(f.width - 1, -1, -1) if flip else range(f.width)
        for xx in cols:
            x = x0 + xx
            if not 0 <= x < w:
                continue
            p = yy * f.width + (f.width - 1 - xx if flip else xx)
            if f.mask[p]:
                yield y * w + x, f.indices[p], yy, (f.width - 1 - xx if flip else xx)


def draw_image(data: Data, buf: bytearray, w: int, h: int, f: grp.Frame, x0: int, y0: int, flip: bool, method: int,
               owner: int, param: int, row: int, banks: list, dark: bytes, shift: bytes, bright: bytes,
               clip_h: int | None = None, trans50: int | None = None) -> None:
    """Draws one image into buf (w x h, row-major). Images write rows below clip_h (default:
    all), the original's 640x400 game viewport in a 640x480 bitmap; distortions read the
    whole buffer. trans50: the player whose colours trans50 rows 8..15 hold (0x416bd0), or
    None while they are unchanged."""
    pmap = data.player_maps[owner % PLAYERS]
    t50map = data.player_maps[trans50 % PLAYERS] if trans50 is not None else IDENTITY
    size = w * h
    clip_h = h if clip_h is None else clip_h
    pixels = list(frame_pixels(f, x0, y0, w, clip_h, flip))
    if method in (2, 3, 4, 8):
        # Distortion in place: each pixel copies the one d bytes ahead (normal) or behind
        # (mirrored) in row-major memory, wrapping at the buffer end (0x4063b5, 0x406533).
        for d, src, _, _ in pixels:
            off = src if method == 8 else bright[src]
            buf[d] = buf[(d - off) % size if flip else (d + off) % size]
    elif method in (5, 6, 7) or (method == 9 and param == TRANS50 + 1):
        for d, src, _, _ in pixels:
            buf[d] = banks[TRANS50][t50map[src] * 256 + buf[d]]
        if method == 9:
            return
    if method in (2, 4, 5, 7):
        # Threshold pass 0x406235: the owner-mapped index where its brightness exceeds the
        # parameter byte (signed BYTE compare).
        limit = signed_byte(param)
        for d, src, _, _ in pixels:
            if limit < bright[pmap[src]]:
                buf[d] = pmap[src]
        return
    if method in (3, 6, 8):
        return
    if method == 12:
        texture = data.grp(WARP_TEXTURE).frames[param]
        for d, src, yy, col in pixels:
            if col < texture.width and yy < texture.height:
                t = texture.indices[yy * texture.width + col]
                if pmap[t] != t:
                    raise CheckError(f"warp texture index {t} differs under the owner's colour map")
                buf[d] = pmap[t]
        return
    if method == 15:
        # Frame rectangle edges only (0x40e9d0), in the consumer's line colour.
        for yy in range(f.height):
            for xx in range(f.width):
                if yy in (0, f.height - 1) or xx in (0, f.width - 1):
                    x, y = x0 + xx, y0 + yy
                    if 0 <= x < w and 0 <= y < clip_h:
                        buf[y * w + x] = row
        return
    for d, src, _, _ in pixels:
        if method in (0, 1):
            buf[d] = pmap[src]
        elif method == 13:
            buf[d] = data.selection_maps[param & 0xFF][src]
        elif method == 14:
            buf[d] = data.player_maps[param % PLAYERS][src]
        elif method == 16:
            buf[d] = shift[src]
        elif method == 9:
            buf[d] = banks[param - 1][src * 256 + buf[d]]
        elif method == 10:
            buf[d] = dark[SHADOW_ROW * 256 + buf[d]]
        elif method == 17:
            buf[d] = banks[0][(param & 0xFF) * 256 + buf[d]]
        else:
            raise CheckError(f"draw method {method} has no CPU kernel here")


def draw_terrain(ts, buf: bytearray, stride: int, view_w: int, view_h: int, cam: tuple[int, int],
                 map_size: tuple[int, int], megatile_at) -> None:
    """Terrain of a view_w x view_h view at cam into buf (rows of `stride` pixels): the
    megatile megatile_at(cell index) & TILE_MASK of each map cell through the tileset."""
    cam_x, cam_y = cam
    map_w, map_h = map_size
    cache: dict[int, bytes] = {}
    for y in range(view_h):
        my = cam_y + y
        if my >= map_h * MEGATILE:
            continue
        for cx in range(cam_x // MEGATILE, min(map_w, (cam_x + view_w + MEGATILE - 1) // MEGATILE)):
            mega = megatile_at((my // MEGATILE) * map_w + cx) & TILE_MASK
            if mega not in cache:
                cache[mega] = ts.render_megatile(mega)
            row = cache[mega][(my % MEGATILE) * MEGATILE:(my % MEGATILE + 1) * MEGATILE]
            x0 = cx * MEGATILE - cam_x
            for k in range(MEGATILE):
                x = x0 + k
                if 0 <= x < view_w:
                    buf[y * stride + x] = row[k]


def compose(data: Data, mission: Path, view: dict) -> bytes:
    ts, raw, banks, dark, shift, bright = data.tileset(view["tileset"])
    c = container.parse(mission.read_bytes())
    meta = c.section("META").payload
    width, height = struct.unpack_from("<HH", meta, 4)
    ids, _ = import_terrain(c.section("MTXM").payload, width, height, raw["cv5"], megatile_flags(raw["vf4"]))
    cam_x, cam_y = view["cam"]
    w, h = view["size"]
    buf = bytearray(w * h)
    draw_terrain(ts, buf, w, w, h, (cam_x, cam_y), (width, height), ids.__getitem__)
    for rank, method, owner, param, path, frame, left, top, flip, row in sorted(view["draws"]):
        if method == HEALTH_BAR:
            # The viewer logs the bar the presentation drew with native/kp_present/kp_health_bar.c,
            # which tests/c/test_present.c checks against the original 0x464d20.
            bw, bh, cells = view["bars"][rank]
            for k, v in enumerate(cells):
                x, y = left - cam_x + k % bw, top - cam_y + k // bw
                if v is not None and 0 <= x < w and 0 <= y < h:
                    buf[y * w + x] = v
            continue
        f = data.grp(path).frames[frame]
        colour = row // COLOR_SPAN if method in (5, 7) else row if method == 6 or (method == 9 and param == TRANS50 + 1) else 0
        draw_image(data, buf, w, h, f, left - cam_x, top - cam_y, flip, method, owner, param, row, banks, dark, shift,
                   bright, trans50=colour - PLAYER_ROW_FIRST if colour else None)
    return bytes(buf)


def report_pixels(view: dict, cpu: bytes) -> None:
    """With a pixel dump (viewer.compose_dump): the first differing pixels and the images there."""
    w, h = view["size"]
    cam_x, cam_y = view["cam"]
    shown = 0
    for y in range(h):
        gpu = view["pixels"].get(y)
        if gpu is None:
            continue
        for x in range(w):
            if gpu[x] != cpu[y * w + x] and shown < 12:
                shown += 1
                over = [d[:3] + d[4:6] for d in view["draws"]
                        if d[6] <= x + cam_x < d[6] + 256 and d[7] <= y + cam_y < d[7] + 256]
                log.info("  pixel %d,%d: CPU %d GPU %d; draws starting near: %s", x, y, cpu[y * w + x], gpu[x], over[-4:])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data", type=Path, required=True)
    p.add_argument("--log", type=Path, default=ROOT / "build" / "spikes" / "viewer" / "web.log")
    p.add_argument("--missions", type=Path, default=ROOT / "generated" / "missions")
    a = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    views = parse_log(a.log)
    if not views:
        raise CheckError(f"no compose lines in {a.log}")
    bad = 0
    with Resolver(a.data, Profile.load(), full_hash=False) as resolver:
        data = Data(resolver)
        for name, view in sorted(views.items()):
            if len(view["draws"]) != view["count"]:
                raise CheckError(f"{name}: {len(view['draws'])} draw lines, compose line says {view['count']}")
            pixels = compose(data, a.missions / f"{name.split('@')[0]}.kpm", view)
            crc = zlib.crc32(pixels) & 0xFFFFFFFF
            ok = crc == view["crc"]
            if not ok and "pixels" in view:
                report_pixels(view, pixels)
            bad += not ok
            overflow = view.get("overflow", 0)
            log.info("%s %s: %d images, CPU %08x GPU %08x%s", "ok  " if ok else "DIFF", name, len(view["draws"]), crc,
                     view["crc"], f" ({overflow} images past a family's capacity)" if overflow else "")
    log.info("compose check: %d views, %d differ", len(views), bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
