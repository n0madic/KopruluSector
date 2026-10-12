#!/usr/bin/env python3
"""Rebuild the map viewer's composed views on the CPU and compare them with the GPU.

The viewer's HTML5 autotest (viewer/viewer.script, check_compose) prints for each mission
the CRC-32 of the index target of its top-left view (rows top-down) and the draw list of
the images meeting that view, once at creation and once after some ticks (<mission>@<ticks>):
  SPIKE compose <mission> <tileset> <cam x> <cam y> <width> <height> <crc> <images>
  SPIKE draw <mission> <rank> <method> <owner> <param> <grp path> <frame> <left> <top> <flip>
This script draws the same view with the original's kernels from the retail files
(docs/research/native-pixel-blitters.md, native-pixel-effects.md), independently of the
engine: terrain megatiles from the tileset, then the images in rank order:
  methods 0/1: owner colour map (tunit row on indices 8..15) over the source index;
  14: the colour map of the player in the drawing parameter; 16: tileset shift.pcx;
  9: blend bank of the images.dat remapping (allocation row = source index) by destination;
  10: dark.pcx row 18 by destination; 17: ofire allocation row = parameter low byte.
It reports, per mission, whether the CRCs match.

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
PLAYER_COLORS = 8  # tunit entries per player, mapped onto indices 8..15
COLOR_FIRST = 8
OVERWRITE_METHODS = {0, 1, 14, 16}
MEGATILE = 32


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
            views[name]["draws"].append((rank, method, owner, param, path_, frame, left, top, flip))
        elif parts[1] == "pixels":
            views[parts[2]].setdefault("pixels", {})[int(parts[3])] = bytes.fromhex(parts[4])
    return views


class Data:
    def __init__(self, resolver: Resolver) -> None:
        self.r = resolver
        self.grps: dict[str, grp.Grp] = {}
        self.tilesets: dict[int, tuple] = {}
        tunit = pcx.decode(resolver.read("game\\tunit.pcx")[1]).pixels
        self.player_maps = []
        for p in range(12):
            m = bytearray(range(256))
            m[COLOR_FIRST:COLOR_FIRST + PLAYER_COLORS] = tunit[p * PLAYER_COLORS:(p + 1) * PLAYER_COLORS]
            self.player_maps.append(bytes(m))

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
            # (zero here, as in palettes.kpd; the original leaves it uninitialised).
            banks = [bytes(256) + table(t) for t in REMAP_TABLES]
            self.tilesets[index] = (ts, raw, banks, table("dark"), table("shift")[:256])
        return self.tilesets[index]


def compose(data: Data, mission: Path, view: dict) -> bytes:
    ts, raw, banks, dark, shift = data.tileset(view["tileset"])
    c = container.parse(mission.read_bytes())
    meta = c.section("META").payload
    width, height = struct.unpack_from("<HH", meta, 4)
    ids, _ = import_terrain(c.section("MTXM").payload, width, height, raw["cv5"], megatile_flags(raw["vf4"]))
    cam_x, cam_y = view["cam"]
    w, h = view["size"]
    buf = bytearray(w * h)
    cache: dict[int, bytes] = {}
    for y in range(h):
        my = cam_y + y
        if my >= height * MEGATILE:
            continue
        for cx in range(cam_x // MEGATILE, min(width, (cam_x + w + MEGATILE - 1) // MEGATILE)):
            mega = ids[(my // MEGATILE) * width + cx] & 0x7FFF
            if mega not in cache:
                cache[mega] = ts.render_megatile(mega)
            row = cache[mega][(my % MEGATILE) * MEGATILE:(my % MEGATILE + 1) * MEGATILE]
            x0 = cx * MEGATILE - cam_x
            for k in range(MEGATILE):
                x = x0 + k
                if 0 <= x < w:
                    buf[y * w + x] = row[k]
    for rank, method, owner, param, path, frame, left, top, flip in sorted(view["draws"]):
        f = data.grp(path).frames[frame]
        for yy in range(f.height):
            y = top + yy - cam_y
            if not 0 <= y < h:
                continue
            for xx in range(f.width):
                x = left + xx - cam_x
                if not 0 <= x < w:
                    continue
                p = yy * f.width + (f.width - 1 - xx if flip else xx)
                if not f.mask[p]:
                    continue
                src, d = f.indices[p], y * w + x
                if method in (0, 1):
                    buf[d] = data.player_maps[owner % 12][src]
                elif method == 14:
                    buf[d] = data.player_maps[param % 12][src]
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
            log.info("%s %s: %d images, CPU %08x GPU %08x", "ok  " if ok else "DIFF", name, len(view["draws"]), crc,
                     view["crc"])
    log.info("compose check: %d views, %d differ", len(views), bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
