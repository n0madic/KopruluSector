#!/usr/bin/env python3
"""Plan phase 8 item 5: Koprulu's fog of war against the original's (atlas P23, P24).

Input: the fog captures of the full-game emulator (tools/research/sc_emulator/fogcap.py): the 640x400 game view
before and after DrawFogOverlay 0x46c3c0, the original's fog buffers (the raw and blurred tile levels, the level map)
and what they came from (the tile flags, the local masks, the camera, the tileset's dark rows). For each capture
Koprulu's fog view (kp_fog_view.h through kp_oracle fog_levels) builds the levels from the same tile flags and camera
and darkens the same view:
1. the raw grid (24 x 17) and the blurred grid's inner cells (rows 1..15, columns 1..22; its border is never written)
   must equal the original's;
2. the level map's cells that BuildFogLevelMap 0x46b910 writes (84 x 56 of its 88 x 60) must equal the original's;
3. the darkened view must equal the original's screen after the fog pass, pixel for pixel.
A minimap capture (minimap*.cap) composes the minimap's fog from its terrain colours and the tile flags of that
moment (kp_fog_minimap) and must equal the original's composed image [0x671fd4]. The tileset's dark rows come from the
fog capture of the same frame. A capture of the game's own
   partial redraw (`forced` false) compares only the 16x16 cells its dirty grid marked (DrawFogOverlayDirty 0x46c3e1
   shades only those; the others keep what an earlier frame drew).

The redraws of a whole run (redraws.cap, fogcap.install_redraws: every fog pass's flag, camera, dirty cells, level map
and tile flags): Koprulu shades the whole view every frame, the original only the dirty cells of a partial redraw, so
the original's screen is Koprulu's only if every 8x8 block a partial redraw leaves still has the four corner levels it
was last shaded with (a block's shading is a function of its pixels and those levels) and the camera of the last full
redraw. The check follows each block through the run; the fog pass must also change no pixel outside the dirty cells,
and Koprulu's level map (from the tile flags) must equal the original's at every redraw where the original's changed.

Wider views (`--wide`, ADR 0009): the original's fog code itself (tools/research/probe_native_fog_117.py, StarCraft.exe
in a bare Unicorn) first redraws each forced capture's view and must leave the emulator's pixels. Then, over the tile
flags of every fog capture and of `--synthetic` generated maps, Koprulu darkens each of WIDE_VIEWS at the corners of
its scroll range, at the capture's camera (two random ones on a generated map, mostly off the 8-pixel grid) and at two
cameras off the grid (mid-range and near its far corner), and every 640 x 400 view of the original that lies over a
part of it (at a camera of the
original's own scroll range, on the 8-pixel grid) must equal that part pixel for pixel. The views of the
original tile the wider one, so the wider view is the original's at every map pixel it shows, and the original's views
agree with one another where they overlap. The pixels under the fog are a pattern of the map pixel and the dark rows
are synthetic (level l takes index i to (i + 8 l + 1) mod 256), so every level leaves another index.

Usage: python3 tools/fog_check.py CAPTURE.cap [...]
       python3 tools/fog_check.py --root /path/to/emu/fog   (every fog*.cap, minimap*.cap and redraws.cap below it)
       python tools/fog_check.py --root DIR --wide --exe DIR/StarCraft.exe [--synthetic N]   (private venv)
"""
from __future__ import annotations

import argparse
import logging
import os
import random
import subprocess
import sys
from pathlib import Path
from typing import TYPE_CHECKING

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "research"))
sys.path.insert(0, str(ROOT / "tools"))

import oracle_cli  # noqa: E402

from sc_emulator.fogcap import REDRAWS, VIEW_H, VIEW_W, FogCapture, redraws  # noqa: E402

if TYPE_CHECKING:
    from probe_native_fog_117 import FogMachine

log = logging.getLogger("fog_check")
FOG_LEVELS = oracle_cli.command("fog_levels")
GRID_COLS, GRID_ROWS = 24, 17
LEVEL_STRIDE = 88
LEVEL_FILLED_W, LEVEL_FILLED_H = 84, 56  # BuildFogLevelMap's 21 x 14 tiles of 4 x 4 blocks
DIRTY_COLS, DIRTY_CELL = 40, 16
BLOCK = 8  # DrawFogOverlay shades 8 x 8 blocks, 2 x 2 to a dirty cell
BLOCK_COLS, BLOCK_ROWS = VIEW_W // BLOCK, VIEW_H // BLOCK
DARK_SIZE = 32 * 256
EXAMPLES = 5  # the problems a run lists
VISION_FIELDS = ("not_visible", "not_explored", "replay", "replay_vision", "replay_reveal")

# Wider views: the original's 640 x 400, a portrait window's narrow view, 16:9 at 480, odd sizes off the 8-pixel
# grid, 1024 x 576 and 1280 x 720 less the console's 80 rows, and a large one.
WIDE_VIEWS = ((640, 400), (502, 880), (854, 400), (1001, 517), (1024, 496), (1280, 640), (1920, 1000))
TILE = 32
VIEW_TILES_H, VIEW_BOTTOM_EXTRA = 12, 8  # InitScreenScroll's (H - 12) * 32 + 8; its (W - 20) * 32 is W * 32 - 640
WIDE_DARK = bytes((i + 8 * level + 1) % 256 for level in range(32) for i in range(256))
PATTERN_MOD, PATTERN_STEP_Y = 251, 468  # (7 x + 13 y) mod 251 = 7 ((x + 468 y) mod 251) mod 251
PATTERN_ROW = bytes(7 * (k % PATTERN_MOD) % PATTERN_MOD for k in range(PATTERN_MOD + 4096))
SYNTHETIC_MAPS = ((64, 64), (96, 96), (128, 128), (192, 192), (256, 256), (128, 64), (64, 192), (256, 96))
SYNTHETIC_SEED = 0x46C3C0


def fog_levels(vision: dict, tiles: bytes, cam_x: int, cam_y: int, w: int, h: int, dark: bytes,
               before: bytes) -> tuple[tuple[int, ...], bytes, bytes, bytes, bytes]:
    """Koprulu's fog of a w x h view: (cols, rows, level_w, level_h), the raw and blurred grids, the level map and
    the pixels `before` darkened."""
    line = " ".join(str(v) for v in (cam_x, cam_y, w, h, *vision["map"], *(vision[k] for k in VISION_FIELDS)))
    p = subprocess.run(FOG_LEVELS, cwd=ROOT, input=line.encode() + b"\n" + tiles + dark + before, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.decode().strip())
    nl = p.stdout.index(b"\n")
    size = tuple(int(x) for x in p.stdout[:nl].split())
    cols, rows, level_w, level_h = size
    body, n, levels = p.stdout[nl + 1:], cols * rows, level_w * level_h
    return size, body[:n], body[n:2 * n], body[2 * n:2 * n + levels], body[2 * n + levels:]


def koprulu(cap: FogCapture) -> tuple[bytes, bytes, bytes, bytes]:
    cam_x, cam_y = cap.header["camera"]
    size, raw, smooth, levels, shaded = fog_levels(cap.header, cap["tiles"], cam_x, cam_y, VIEW_W, VIEW_H, cap["dark"],
                                                   cap["before"])
    if size[:3] != (GRID_COLS, GRID_ROWS, LEVEL_STRIDE):
        raise RuntimeError("grids of {}x{}, level map {}x{}".format(*size))
    return raw, smooth, levels, shaded


def koprulu_minimap(cap: FogCapture, dark: bytes) -> bytes:
    h = cap.header
    line = " ".join(str(v) for v in ("minimap", *h["map"], *(h[k] for k in VISION_FIELDS), cap["unexplored"][0],
                                      len(cap["terrain"])))
    request = line.encode() + b"\n" + cap["tiles"] + cap["terrain"] + dark
    p = subprocess.run(FOG_LEVELS, cwd=ROOT, input=request, capture_output=True)
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
    if len(a) != len(b):  # equal up to the shorter one
        return f"size: original {len(a)} koprulu {len(b)}"
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


# --- the original's partial redraws against Koprulu's whole frames ---

def block_corners(levels: bytes, cam_x: int, cam_y: int) -> list[tuple[bytes, bytes, bytes, bytes]]:
    """Per block row of the view, the top-left, top-right, bottom-left and bottom-right levels of its 80 blocks: the
    level map from the camera's block in its tile (DrawFogOverlay 0x46c3c0, DrawFogOverlayDirty 0x46c3e1)."""
    bx, by = (cam_x >> 3) & 3, (cam_y >> 3) & 3
    rows = []
    for j in range(BLOCK_ROWS):
        top, bottom = (by + j) * LEVEL_STRIDE + bx, (by + j + 1) * LEVEL_STRIDE + bx
        rows.append((levels[top:top + BLOCK_COLS], levels[top + 1:top + 1 + BLOCK_COLS],
                     levels[bottom:bottom + BLOCK_COLS], levels[bottom + 1:bottom + 1 + BLOCK_COLS]))
    return rows


def filled(levels: bytes) -> bytes:
    """The level map's cells BuildFogLevelMap writes."""
    return b"".join(levels[r * LEVEL_STRIDE:r * LEVEL_STRIDE + LEVEL_FILLED_W] for r in range(LEVEL_FILLED_H))


class ShadedView:
    """The original's view through its redraws: the corner levels each 8 x 8 block was last shaded with (a full redraw
    shades every block, a partial one the blocks of its dirty cells) and the camera of the last full redraw."""

    def __init__(self) -> None:
        self.rows: list[list[bytearray]] | None = None
        self.camera: tuple[int, int] | None = None

    def redraw(self, full: bool, camera: tuple[int, int], levels: bytes, dirty: bytes) -> list[tuple[int, int, tuple,
                                                                                                    tuple]]:
        """Shade as the original does; the blocks a partial redraw left whose levels changed since they were shaded
        ((x, y, levels then, levels now), the corners top-left, top-right, bottom-left, bottom-right)."""
        corners = block_corners(levels, *camera)
        if full:
            self.rows, self.camera = [[bytearray(c) for c in row] for row in corners], camera
            return []
        stale = []
        for j, row in enumerate(corners):
            last, cells = self.rows[j], dirty[j // 2 * DIRTY_COLS:(j // 2 + 1) * DIRTY_COLS]
            for cx in range(DIRTY_COLS):
                if cells[cx]:
                    for k in range(4):
                        last[k][2 * cx:2 * cx + 2] = row[k][2 * cx:2 * cx + 2]
            if any(last[k] != row[k] for k in range(4)):
                for i in range(BLOCK_COLS):
                    then, now = tuple(last[k][i] for k in range(4)), tuple(row[k][i] for k in range(4))
                    if then != now:
                        stale.append((i * BLOCK, j * BLOCK, then, now))
        return stale


def run_name(path: Path) -> str:
    """A run's name: the folder of its redraws.cap, or of that folder in a play's output (<play>/fog/redraws.cap)."""
    return path.parent.parent.name if path.parent.name == "fog" else path.parent.name


def check_redraws(path: Path) -> bool:
    view = ShadedView()
    previous = None
    records = full = stale = moved = outside = compared = levels_bad = 0
    frames: set[int] = set()
    problems: list[str] = []
    blank, dark = bytes(VIEW_W * VIEW_H), bytes(DARK_SIZE)
    for h, blobs in redraws(path):
        records += 1
        frames.add(h["frame"])
        cam, levels = tuple(h["camera"]), blobs["levels"]
        if (filled(levels), cam) != previous:
            previous = filled(levels), cam
            compared += 1
            mine = fog_levels(h, blobs["tiles"], *cam, VIEW_W, VIEW_H, dark, blank)[3]
            if filled(mine) != previous[0]:
                levels_bad += 1
                problems.append(f"frame {h['frame']}: levels " + str(first_difference(levels, mine, LEVEL_STRIDE)))
        if h["full"]:
            full += 1
        elif view.rows is None:  # recorded before the first full redraw
            continue
        else:
            if cam != view.camera:
                moved += 1
                problems.append(f"frame {h['frame']}: partial redraw at camera {cam}, the last full one at "
                                f"{view.camera}")
            if h["outside"]:
                outside += h["outside"]
                problems.append(f"frame {h['frame']}: {h['outside']} pixels changed outside the dirty cells")
        for x, y, then, now in view.redraw(bool(h["full"]), cam, levels, blobs["dirty"]):
            stale += 1
            if stale <= EXAMPLES:
                problems.append(f"frame {h['frame']}: block ({x}, {y}) keeps levels {then}, now {now}")
    ok = records > full > 0 and not problems
    span = max(frames) - min(frames) + 1 if frames else 0
    log.info("%s: %s; %d redraws (%d full) over frames %d..%d, %d frames without one; %d stale blocks, %d partial "
             "redraws after a camera move, %d pixels outside the dirty cells; Koprulu's level map differs at %d of "
             "the %d changes", run_name(path), "equal" if ok else "DIFF", records, full, min(frames, default=0),
             max(frames, default=0), span - len(frames), stale, moved, outside, levels_bad, compared)
    for problem in problems[:EXAMPLES * 4]:
        log.info("  DIFF %s", problem)
    return ok


# --- wider views against the original's (ADR 0009) ---

def pattern(x0: int, y0: int, w: int, h: int) -> bytes:
    """The pixels under the fog of the w x h view at map pixel (x0, y0): (7 x + 13 y) mod 251 of each map pixel."""
    rows = []
    for y in range(y0, y0 + h):
        at = (x0 + PATTERN_STEP_Y * y) % PATTERN_MOD
        rows.append(PATTERN_ROW[at:at + w])
    return b"".join(rows)


def on_grid(v: int) -> int:
    return v & ~7 if v > 0 else 0


def scroll_range(map_w: int, map_h: int, w: int, h: int) -> tuple[int, int]:
    """The largest camera of a w x h view off the grid, negative for a view larger than the map (kp_ui_scroll.c: the
    original's limits less the view's extra size); the original's own for 640 x 400."""
    return map_w * TILE - w, map_h * TILE - VIEW_TILES_H * TILE - (h - VIEW_H) + VIEW_BOTTOM_EXTRA


def scroll_limits(map_w: int, map_h: int, w: int, h: int) -> tuple[int, int]:
    """The largest camera of a w x h view, on the 8-pixel grid."""
    max_x, max_y = scroll_range(map_w, map_h, w, h)
    return on_grid(max_x), on_grid(max_y)


def cover(start: int, size: int, view: int, top: int) -> list[int]:
    """The original's view positions (on the 8-pixel grid, 0..top) that cover start..start + size together."""
    out, x = [], on_grid(start)
    while x + view < start + size:
        out.append(min(x, top))
        x += view
    out.append(min(max((start + size - view + 7) & ~7, 0), top))
    if out[0] > start or out[-1] + view < start + size:
        raise RuntimeError(f"no cover of {start}..{start + size} by views of {view} up to {top}")
    return out


def view_difference(original: bytes, wx: int, wy: int, mine: bytes, cam_x: int, cam_y: int, w: int,
                    h: int) -> str | None:
    """The first pixel where the original's view at (wx, wy) differs from the wider view at (cam_x, cam_y)."""
    a, b = max(0, cam_x - wx), min(VIEW_W, cam_x + w - wx)
    for y in range(max(0, cam_y - wy), min(VIEW_H, cam_y + h - wy)):
        seg = original[y * VIEW_W + a:y * VIEW_W + b]
        at = (wy + y - cam_y) * w + wx + a - cam_x
        ref = mine[at:at + b - a]
        if seg != ref:
            k = next(i for i in range(len(seg)) if seg[i] != ref[i])
            return (f"map pixel ({wx + a + k}, {wy + y}) of the original's view at ({wx}, {wy}): original {seg[k]} "
                    f"koprulu {ref[k]}")
    return None


def check_wide_source(machine: FogMachine, name: str, vision: dict, tiles: bytes, extra: list[tuple[int, int]]) -> int:
    """Every wider view over one map's tile flags; the number of view sizes with a view that differs."""
    map_w, map_h = vision["map"]
    machine.load(vision, tiles, WIDE_DARK)
    top_x, top_y = scroll_limits(map_w, map_h, VIEW_W, VIEW_H)
    originals: dict[tuple[int, int], bytes] = {}  # the original's view at a camera depends on the map alone
    sizes = cameras_seen = views = 0
    problems = []
    for w, h in WIDE_VIEWS:
        if min(scroll_range(map_w, map_h, w, h)) < 0:  # past the map: no view of the original shows it
            continue
        max_x, max_y = scroll_limits(map_w, map_h, w, h)
        cameras = {(0, 0), (max_x, 0), (0, max_y), (max_x, max_y)}
        cameras |= {(min(x, max_x), min(y, max_y)) for x, y in extra}
        # cameras off the grid (a finger's pan): 29 and 27 pixels into a tile mid-range, the view's last block one past
        # a grid camera's; and near the range's far corner, where the grids' extra tile meets the map's edge
        cameras.add((min((max_x // 2 & ~31) + 29, max_x), min((max_y // 2 & ~31) + 27, max_y)))
        cameras.add((max(max_x - 3, 0), max(max_y - 5, 0)))
        problem = None
        for cam_x, cam_y in sorted(cameras):
            mine = fog_levels(vision, tiles, cam_x, cam_y, w, h, WIDE_DARK, pattern(cam_x, cam_y, w, h))[4]
            for wy in cover(cam_y, h, VIEW_H, top_y):
                for wx in cover(cam_x, w, VIEW_W, top_x):
                    original = originals.get((wx, wy))
                    if original is None:
                        original = originals[wx, wy] = machine.view(wx, wy, pattern(wx, wy, VIEW_W, VIEW_H))
                    views += 1
                    diff = view_difference(original, wx, wy, mine, cam_x, cam_y, w, h)
                    if diff and problem is None:
                        problem = f"{w}x{h} at camera ({cam_x}, {cam_y}): {diff}"
        sizes += 1
        cameras_seen += len(cameras)
        if problem is not None:
            problems.append(problem)
    log.info("%s %dx%d: %d view sizes, %d cameras, %d original views (%d run) %s", name, map_w, map_h, sizes,
             cameras_seen, views, len(originals), "equal" if not problems else "DIFF")
    for problem in problems:
        log.info("  DIFF %s", problem)
    return len(problems)


def synthetic_map(rng: random.Random) -> tuple[dict, bytes]:
    """A map's tile flags: unexplored, explored and seen rectangles and single tiles of the local players' masks (an
    allied vision now and then), the other players' bits and the high WORD random; or a replay's random flags."""
    map_w, map_h = rng.choice(SYNTHETIC_MAPS)
    mask = 1 << rng.randrange(8)
    if rng.random() < 0.3:
        mask |= 1 << rng.randrange(8)
    vision = {"map": [map_w, map_h], "not_visible": mask, "not_explored": mask << 8, "replay": 0, "replay_vision": 0,
              "replay_reveal": 0}
    if rng.random() < 0.25:
        vision.update(replay=1, replay_vision=rng.randrange(1, 256), replay_reveal=int(rng.random() < 0.2))
        return vision, b"".join(rng.getrandbits(32).to_bytes(4, "little") for _ in range(map_w * map_h))
    state = [0] * (map_w * map_h)  # 0 unexplored, 1 explored, 2 seen
    for level in (1, 1, 2):
        for _ in range(rng.randrange(4, 16)):
            rw, rh = rng.randrange(1, 24), rng.randrange(1, 24)
            x0, y0 = rng.randrange(map_w), rng.randrange(map_h)
            for y in range(y0, min(map_h, y0 + rh)):
                for x in range(x0, min(map_w, x0 + rw)):
                    state[y * map_w + x] = max(state[y * map_w + x], level)
    for _ in range(map_w * map_h // 64):
        state[rng.randrange(map_w * map_h)] = rng.randrange(3)
    bits = [b for b in range(8) if mask >> b & 1]
    out = []
    for s in state:
        f = rng.getrandbits(32) & ~(mask | mask << 8)
        if s == 0:
            f |= sum(1 << (b + 8) for b in bits if rng.random() < 0.5) or 1 << (bits[0] + 8)
            f |= mask & rng.getrandbits(8)
        elif s == 1:
            f |= sum(1 << b for b in bits if rng.random() < 0.5) or 1 << bits[-1]
        out.append(f.to_bytes(4, "little"))
    return vision, b"".join(out)


def check_wide(exe: Path, captures: list[Path], synthetic: int, seed: int) -> tuple[int, int]:
    """(sources, sources with a view that differs) over the fog captures' tile flags and the synthetic maps."""
    from probe_native_fog_117 import FogMachine

    machine = FogMachine(exe.read_bytes())
    sources = bad = 0
    for path in captures:
        cap = FogCapture(path)
        h = cap.header
        sources += 1
        if h["forced"]:  # the bare machine redraws what the full-game emulator drew
            machine.load(h, cap["tiles"], cap["dark"])
            if machine.view(h["camera"][0], h["camera"][1], cap["before"]) != cap["after"]:
                log.info("%s: the original's fog code redraws another view than the emulator's", path.name)
                bad += 1
                continue
        bad += check_wide_source(machine, f"{path.parent.name}/{path.name}", h, cap["tiles"], [tuple(h["camera"])]) > 0
    rng = random.Random(seed)
    for k in range(synthetic):
        vision, tiles = synthetic_map(rng)
        map_w, map_h = vision["map"]
        extra = [(rng.randrange(map_w * TILE), rng.randrange(map_h * TILE)) for _ in range(2)]
        sources += 1
        bad += check_wide_source(machine, f"synthetic {k}", vision, tiles, extra) > 0
    return sources, bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("captures", nargs="*", type=Path)
    ap.add_argument("--root", type=Path)
    ap.add_argument("--wide", action="store_true", help="the wider views against the original's (needs --exe)")
    ap.add_argument("--exe", type=Path, help="StarCraft.exe 1.17.0.1 (default: $KOPRULU_ORIGINAL/StarCraft.exe)")
    ap.add_argument("--synthetic", type=int, default=16, help="synthetic maps of --wide (default 16)")
    ap.add_argument("--seed", type=int, default=SYNTHETIC_SEED)
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    given = list(a.captures) + (sorted(a.root.rglob("fog*.cap")) + sorted(a.root.rglob("minimap*.cap")) if a.root else [])
    runs = [p for p in given if p.name == REDRAWS] + (sorted(a.root.rglob(REDRAWS)) if a.root else [])
    paths = [p for p in given if p.name != REDRAWS]
    bad = sum(0 if (check_minimap(p) if p.name.startswith("minimap") else check(p)) else 1 for p in paths)
    print(f"fog_check: {len(paths)} fog and minimap captures, {bad} differ")
    if runs:
        runs_bad = sum(0 if check_redraws(p) else 1 for p in runs)
        print(f"fog_check: {len(runs)} runs' redraws, {runs_bad} differ")
        bad += runs_bad
    if a.wide:
        original = os.environ.get("KOPRULU_ORIGINAL")
        exe = a.exe or (Path(original) / "StarCraft.exe" if original else None)
        if exe is None:
            ap.error("--wide needs --exe or KOPRULU_ORIGINAL")
        fog = [p for p in paths if p.name.startswith("fog")]
        sources, wide_bad = check_wide(exe, fog, a.synthetic, a.seed)
        print(f"fog_check --wide: {sources} maps ({len(fog)} captures, {a.synthetic} synthetic), {wide_bad} differ")
        return 0 if paths and sources and bad == 0 and wide_bad == 0 else 1
    return 0 if paths and bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
