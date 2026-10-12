#!/usr/bin/env python3
"""Phase 2 acceptance: our draw list and kernels against the original renderer's frames.

Input: captures of the full-game emulator (tools/research/sc_emulator/capture.py): the
original's 640x480 screen right after DrawSortedSprites 0x415b30 in a full redraw, with the
image and sprite records, rows, camera, vision mask and the megatile array it drew.
For each capture:
1. the records become a game.images world and presentation/draw_list.lua orders them for
   the captured view (tools/lua/capture_draw_list.lua, LuaJIT);
2. the images are composed on the CPU over the original's own terrain (the screen captured
   at the call of DrawSortedSprites) in rank order with the original kernels
   (tools/viewer_compose_check.py, which the viewer's GPU output already equals), and the
   result is compared with the captured screen over the 640x400 game viewport (the rows
   below hold the previous interface and are only read by distortions): every pixel must
   be equal;
3. separately, the terrain drawn from the captured megatile array through the tileset is
   compared with the original's terrain; creep edges (drawn over the tiles from the creep
   state, phase 3) are expected to differ and are only reported.

Usage: python3 tools/compose_capture.py --data RETAIL_DIR CAPTURE.cap [...]
       python3 tools/compose_capture.py --data RETAIL_DIR --root /path/to/emu/captures
"""
from __future__ import annotations

import argparse
import logging
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "research"))

import viewer_compose_check as vc  # noqa: E402
from converter.mpq.profile import Profile, Resolver  # noqa: E402
from sc_emulator import capture  # noqa: E402

log = logging.getLogger("compose_capture")

VIEW_W, VIEW_H = 640, 400  # the original's game viewport (images clip to it)
IMAGE_SIZE, SPRITE_SIZE = capture.IMAGE_SIZE, capture.SPRITE_SIZE
POOL_FIRST = {"main": 0, "health": 5000, "circles": 5012, "ghosts": 5092, "boxes": 5156}  # game/images/core.lua
NONE = -1
LUA = "luajit"
VIRTUAL = "virtual"
METHOD_REMAP = 9


def records(header: dict, blobs: dict[str, bytes]) -> str:
    """The captured records as a Lua table for tools/lua/capture_draw_list.lua."""
    pools = []
    for name, base, count in header["pools"]:
        pools.append((base, base + IMAGE_SIZE * count, POOL_FIRST[name], blobs["images_" + name]))
    sprites_base = header["sprites_base"]
    sprites = blobs["sprites"]

    def image_key(p: int) -> int:
        if p == 0:
            return NONE
        for lo, hi, first, _ in pools:
            if lo <= p < hi:
                return first + (p - lo) // IMAGE_SIZE
        raise ValueError(f"image pointer {p:#x} outside the pools")

    def image_bytes(key: int) -> bytes:
        for lo, hi, first, data in pools:
            n = (hi - lo) // IMAGE_SIZE
            if first <= key < first + n:
                o = (key - first) * IMAGE_SIZE
                return data[o:o + IMAGE_SIZE]
        raise ValueError(key)

    def sprite_key(p: int) -> int:
        return NONE if p == 0 else (p - sprites_base) // SPRITE_SIZE

    # A method-9 image's parameter is the data pointer of its remap record (0x508828); the
    # draw list wants the record index (images.dat remapping 1..7).
    remap_index = {}
    records_blob = blobs["remap_records"]
    for k in range(capture.REMAP_COUNT):
        ptr = struct.unpack_from("<I", records_blob, k * capture.REMAP_RECORD_SIZE + 4)[0]
        if ptr:
            remap_index.setdefault(ptr, k)
    vgrp = {}
    for name in ("health", "boxes"):
        base, data = header["vgrp"][name], blobs["vgrp_" + name]
        for k in range(len(data) // capture.VGRP_SIZE):
            vgrp[base + k * capture.VGRP_SIZE] = data[k * capture.VGRP_SIZE:(k + 1) * capture.VGRP_SIZE]

    lines = ["return {", f"\tmap = {{ {header['map'][0]}, {header['map'][1]} }},", f"\tvision = {header['vision']},",
             f"\tcamera = {{ {header['camera'][0]}, {header['camera'][1]} }},", f"\tview = {{ {VIEW_W}, {VIEW_H} }},",
             "\trows = {"]
    heads, tails = blobs["row_heads"], blobs["row_tails"]
    live = []
    for r in range(header["map"][1]):
        h, t = struct.unpack_from("<I", heads, 4 * r)[0], struct.unpack_from("<I", tails, 4 * r)[0]
        lines.append(f"\t\t{{ {sprite_key(h)}, {sprite_key(t)} }},")
        p = h
        while p:
            live.append(sprite_key(p))
            p = struct.unpack_from("<I", sprites, sprite_key(p) * SPRITE_SIZE + 4)[0]
    lines.append("\t},")
    lines.append("\tsprites = {")
    images = []
    for s in live:
        b = sprites[s * SPRITE_SIZE:(s + 1) * SPRITE_SIZE]
        prev, nxt, sid, owner, sel, vis, elev, flags, timer, index, width, height, x, y, main, head, tail = \
            struct.unpack_from("<IIHBBBBBBHBBhhIII", b)
        lines.append(f"\t\t{{ key = {s}, prev = {sprite_key(prev)}, next = {sprite_key(nxt)}, id = {sid}, "
                     f"owner = {owner}, selection = {sel}, visibility = {vis}, elevation = {elev}, flags = {flags}, "
                     f"sel_timer = {timer}, index = {index}, width = {width}, height = {height}, x = {x % 65536}, "
                     f"y = {y % 65536}, main = {image_key(main)}, head = {image_key(head)}, tail = {image_key(tail)} }},")
        p = head
        while p:
            images.append(image_key(p))
            p = struct.unpack_from("<I", image_bytes(image_key(p)), 4)[0]
    lines.append("\t},")
    lines.append("\timages = {")
    for k in images:
        b = image_bytes(k)
        prev, nxt, iid, method, direction, flags, xoff, yoff, hdr, pc, ret, anim, wait, frameset, frame, x, y = \
            struct.unpack_from("<IIHBBHbbHHHBBHHhh", b)
        grp_ptr, param, sprite = struct.unpack_from("<II", b, 0x2C)[0], struct.unpack_from("<I", b, 0x30)[0], \
            struct.unpack_from("<I", b, 0x3C)[0]
        if method == METHOD_REMAP:
            param = remap_index[param]
        virtual = ""
        if grp_ptr in vgrp:
            v = vgrp[grp_ptr]
            virtual = (f", virtual = {{ width = {struct.unpack_from('<H', v, 2)[0]}, height = {struct.unpack_from('<H', v, 4)[0]}, "
                       f"fw = {v[8]}, fh = {v[9]} }}")
        lines.append(f"\t\t{{ key = {k}, prev = {image_key(prev)}, next = {image_key(nxt)}, id = {iid}, method = {method}, "
                     f"dir = {direction}, flags = {flags}, xoff = {xoff}, yoff = {yoff}, header = {hdr}, pc = {pc}, "
                     f"ret = {ret}, anim = {anim}, wait = {wait}, frameset = {frameset}, frame = {frame}, x = {x}, "
                     f"y = {y}, param = {param}, sprite = {sprite_key(sprite)}{virtual} }},")
    lines += ["\t},", "}", ""]
    return "\n".join(lines)


def draw_list(lua_records: str) -> list[tuple]:
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
        f.write(lua_records)
        path = f.name
    try:
        out = subprocess.run([LUA, "tools/lua/capture_draw_list.lua", path], cwd=ROOT, check=True,
                             capture_output=True, text=True).stdout
    finally:
        Path(path).unlink()
    draws = []
    for line in out.splitlines():
        parts = line.split()
        if parts[0] == "draw":
            draws.append((*map(int, parts[1:5]), parts[5], *map(int, parts[6:11])))
    return draws


def terrain(data: vc.Data, header: dict, blobs: dict[str, bytes]) -> bytes:
    """The captured view's terrain from its megatile array (rows below the viewport: the
    original's)."""
    ts = data.tileset(header["tileset"])[0]
    tiles = blobs["tiles"]
    buf = bytearray(blobs["terrain"])
    vc.draw_terrain(ts, buf, header["screen"][0], VIEW_W, VIEW_H, tuple(header["camera"]), tuple(header["map"]),
                    lambda cell: struct.unpack_from("<H", tiles, 2 * cell)[0])
    return bytes(buf)


def compose(data: vc.Data, header: dict, blobs: dict[str, bytes], draws: list[tuple]) -> bytes:
    """The images over the original's terrain screen."""
    _, _, banks, dark, shift, bright = data.tileset(header["tileset"])
    w, h = header["screen"]
    cam_x, cam_y = header["camera"]
    buf = bytearray(blobs["terrain"])
    for rank, method, owner, param, path, frame, left, top, flip, trans50 in sorted(draws):
        if path == VIRTUAL:
            raise vc.CheckError(f"virtual image (method {method}) in a capture: not supported yet")
        f = data.grp(path).frames[frame]
        vc.draw_image(data, buf, w, h, f, left - cam_x, top - cam_y, flip == 1, method, owner, param, 0, banks, dark,
                      shift, bright, clip_h=VIEW_H, trans50=trans50 if trans50 >= 0 else None)
    return bytes(buf)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data", type=Path, required=True)
    p.add_argument("--root", type=Path, help="emulator capture root (<mission>/captures/*.cap)")
    p.add_argument("captures", nargs="*", type=Path)
    a = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    paths = list(a.captures) + (sorted(a.root.glob("*/captures/*.cap")) if a.root else [])
    bad, images, terrain_views = 0, 0, 0
    with Resolver(a.data, Profile.load(), full_hash=False) as resolver:
        data = vc.Data(resolver)
        for path in paths:
            header, blobs = capture.read(path)
            draws = draw_list(records(header, blobs))
            cpu = compose(data, header, blobs, draws)
            w = header["screen"][0]
            n = w * VIEW_H
            diff = [(i % w, i // w) for i in range(n) if cpu[i] != blobs["screen"][i]]
            ground = terrain(data, header, blobs)
            terrain_diff = sum(1 for i in range(n) if ground[i] != blobs["terrain"][i])
            bad += bool(diff)
            images += len(draws)
            terrain_views += bool(terrain_diff)
            log.info("%s %s: %d images, %d of %d pixels differ%s; terrain: %d pixels differ",
                     "ok  " if not diff else "DIFF", f"{path.parent.parent.name}/{path.stem}", len(draws), len(diff), n,
                     f", first at {diff[:4]}" if diff else "", terrain_diff)
    log.info("captures: %d (%d images), %d differ; terrain differs in %d (creep edges)", len(paths), images, bad,
             terrain_views)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
