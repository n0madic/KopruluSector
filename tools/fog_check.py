#!/usr/bin/env python3
"""Plan phase 8 item 5: Koprulu's fog of war against the original's (atlas P23, P24).

Input: the fog captures of the full-game emulator (tools/research/sc_emulator/fogcap.py): the 640x400 game view
before and after DrawFogOverlay 0x46c3c0, the original's fog buffers (the raw and blurred tile levels, the level map)
and what they came from (the tile flags, the local masks, the camera, the tileset's dark rows). For each capture
Koprulu's fog view (kp_core.game.fog_view, tools/lua/fog_levels.lua) builds the levels from the same tile flags and
camera and darkens the same view:
1. the raw grid (24 x 17) and the blurred grid's inner cells (rows 1..15, columns 1..22; its border is never written)
   must equal the original's;
2. the level map's cells that BuildFogLevelMap 0x46b910 writes (84 x 56 of its 88 x 60) must equal the original's;
3. the darkened view must equal the original's screen after the fog pass, pixel for pixel.
A minimap capture (minimap*.cap) composes the minimap's fog from its terrain colours and the tile flags of that
moment (kp_fog_minimap) and must equal the original's composed image [0x671fd4]. The tileset's dark rows come from the
fog capture of the same frame. A capture of the game's own
   partial redraw (`forced` false) compares only the 16x16 cells its dirty grid marked (DrawFogOverlayDirty 0x46c3e1
   shades only those; the others keep what an earlier frame drew).

Usage: python3 tools/fog_check.py CAPTURE.cap [...]
       python3 tools/fog_check.py --root /path/to/emu/fog   (every fog*.cap below it)
"""
from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "research"))

from sc_emulator.fogcap import VIEW_H, VIEW_W, FogCapture  # noqa: E402

log = logging.getLogger("fog_check")
LUA = "luajit"
GRID_COLS, GRID_ROWS = 24, 17
LEVEL_STRIDE = 88
LEVEL_FILLED_W, LEVEL_FILLED_H = 84, 56  # BuildFogLevelMap's 21 x 14 tiles of 4 x 4 blocks
DIRTY_COLS, DIRTY_CELL = 40, 16


def koprulu(cap: FogCapture) -> tuple[bytes, bytes, bytes, bytes]:
    h = cap.header
    line = " ".join(str(v) for v in (*h["camera"], VIEW_W, VIEW_H, *h["map"], h["not_visible"], h["not_explored"],
                                      h["replay"], h["replay_vision"], h["replay_reveal"]))
    request = line.encode() + b"\n" + cap["tiles"] + cap["dark"] + cap["before"]
    p = subprocess.run([LUA, "tools/lua/fog_levels.lua"], cwd=ROOT, input=request, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.decode().strip())
    nl = p.stdout.index(b"\n")
    cols, rows, level_w, level_h = (int(x) for x in p.stdout[:nl].split())
    if (cols, rows, level_w) != (GRID_COLS, GRID_ROWS, LEVEL_STRIDE):
        raise RuntimeError(f"grids of {cols}x{rows}, level map {level_w}x{level_h}")
    body, n = p.stdout[nl + 1:], cols * rows
    return body[:n], body[n:2 * n], body[2 * n:2 * n + level_w * level_h], body[2 * n + level_w * level_h:]


def koprulu_minimap(cap: FogCapture, dark: bytes) -> bytes:
    h = cap.header
    line = " ".join(str(v) for v in ("minimap", *h["map"], h["not_visible"], h["not_explored"], h["replay"],
                                      h["replay_vision"], h["replay_reveal"], cap["unexplored"][0], len(cap["terrain"])))
    request = line.encode() + b"\n" + cap["tiles"] + cap["terrain"] + dark
    p = subprocess.run([LUA, "tools/lua/fog_levels.lua"], cwd=ROOT, input=request, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.decode().strip())
    return p.stdout


def check_minimap(path: Path) -> bool:
    cap = FogCapture(path)
    fog_path = path.with_name(path.name.replace("minimap", "fog"))
    if not fog_path.exists():  # the tileset's dark rows come from the fog capture of the same frame
        log.info("%s: no fog capture %s for its dark rows", path.name, fog_path.name)
        return False
    fog = FogCapture(fog_path)
    mine = koprulu_minimap(cap, fog["dark"])
    width = cap.header["image_size"][0]
    diff = first_difference(cap["image"], mine, width) if mine != cap["image"] else None
    log.info("%s (composed at frame %d): %s", path.name, cap.header["at_frame"], "equal" if not diff else "DIFF " + diff)
    return diff is None


def first_difference(a: bytes, b: bytes, width: int) -> str | None:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"({i % width}, {i // width}): original {x} koprulu {y}"
    return None


def check(path: Path) -> bool:
    cap = FogCapture(path)
    raw, smooth, levels, shaded = koprulu(cap)
    problems = []
    if raw != cap["raw"]:
        problems.append("raw " + str(first_difference(cap["raw"], raw, GRID_COLS)))
    inner = [(r, c) for r in range(1, GRID_ROWS - 1) for c in range(1, GRID_COLS - 1)]
    if any(smooth[r * GRID_COLS + c] != cap["smooth"][r * GRID_COLS + c] for r, c in inner):
        problems.append("smooth")
    filled = [r * LEVEL_STRIDE + c for r in range(LEVEL_FILLED_H) for c in range(LEVEL_FILLED_W)]
    if any(levels[i] != cap["levels"][i] for i in filled):
        problems.append("levels")
    after = cap["after"]
    if cap.header["forced"]:
        if shaded != after:
            problems.append("pixels " + str(first_difference(after, shaded, VIEW_W)))
    else:
        dirty = cap["dirty"]
        for y in range(VIEW_H):
            for x in range(VIEW_W):
                if dirty[(y // DIRTY_CELL) * DIRTY_COLS + x // DIRTY_CELL] and shaded[y * VIEW_W + x] != after[y * VIEW_W + x]:
                    problems.append(f"pixels ({x}, {y}): original {after[y * VIEW_W + x]} koprulu {shaded[y * VIEW_W + x]}")
                    break
            if problems and problems[-1].startswith("pixels"):
                break
    darkened = sum(1 for a, b in zip(cap["before"], after) if a != b)
    log.info("%s camera %s: %s (%d pixels darkened)", path.name, cap.header["camera"],
             "equal" if not problems else "DIFF " + "; ".join(problems), darkened)
    return not problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("captures", nargs="*", type=Path)
    ap.add_argument("--root", type=Path)
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    paths = list(a.captures) + (sorted(a.root.rglob("fog*.cap")) + sorted(a.root.rglob("minimap*.cap")) if a.root else [])
    bad = sum(0 if (check_minimap(p) if p.name.startswith("minimap") else check(p)) else 1 for p in paths)
    log.info("%d fog and minimap captures, %d differ", len(paths), bad)
    return 0 if paths and bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
