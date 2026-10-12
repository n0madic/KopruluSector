"""Screen captures for the phase 2 acceptance (plan §7 phase 2, docs/phase2.md).

In each capture frame the game's draw procedure DrawGameScreen 0x445ea0 runs with its full
terrain redraw flag (byte 0x6d0bf5 bit 0) set and every cell of the 16x16 dirty grid
0x6d06d0 marked (DrawSortedSprites draws an image only where a cell is dirty, 0x417790), so
every tile and image is redrawn; right after
DrawSortedSprites 0x415b30 returns (0x445efc), before the fog pass 0x46c3c0, the platform
stars and the interface, the capture stores:
- the 640x480 screen bitmap (0x6d0b88), and as `terrain` the same bitmap at the call of
  DrawSortedSprites (0x445ef7), when it holds the terrain with its creep edges only;
- the camera (0x543218/0x54321c), map size (0x5264b4/0x5264b6) and the sprite vision mask
  (0x672668, which decides the sprites drawn, 0x416a00);
- every image record (main pool 0x544698, health bars 0x5928a0, circles 0x592c50, ghosts
  0x594070, boxes 0x595078) and sprite record (0x5af238), the row lists (0x5c51d0/0x5c55d0)
  and the virtual GRP headers of health bars and boxes;
- the megatile array the terrain is drawn from ([0x54323c], W*H WORDs);
- the remap records 0x508828 (a method-9 image's parameter is its table's data pointer).
File: <out>/f<frame>.cap = JSON header line, then the blobs it lists (name, offset, size).
tools/compose_capture.py turns a capture into the draw list of presentation/draw_list.lua and
compares the CPU composition with the captured bitmap.
"""
from __future__ import annotations

import json
import logging
from pathlib import Path

from . import game, render

log = logging.getLogger(__name__)

DRAW_GAME, BEFORE_SPRITES, AFTER_SPRITES = 0x445EA0, 0x445EF7, 0x445EFC
REDRAW_FLAGS, FULL_REDRAW = 0x6D0BF5, 0x01
DIRTY_GRID, DIRTY_CELLS = 0x6D06D0, 40 * 30  # 16x16-pixel cells of the 640x480 screen
CAMERA_X, CAMERA_Y = 0x543218, 0x54321C
MAP_W, MAP_H = 0x5264B4, 0x5264B6
MAP_TILES_PTR = 0x54323C
VISION = 0x672668
TILESET = 0x5264BC
IMAGE_SIZE, SPRITE_SIZE = 0x40, 0x24
POOLS = (("main", 0x544698, 5000), ("health", 0x5928A0, 12), ("circles", 0x592C50, 80),
         ("ghosts", 0x594070, 64), ("boxes", 0x595078, 64))
SPRITES, SPRITE_COUNT = 0x5AF238, 2500
ROW_HEADS, ROW_TAILS, ROWS = 0x5C51D0, 0x5C55D0, 256
VGRP_HEALTH, VGRP_BOXES, VGRP_SIZE = 0x592BA8, 0x59608E, 0xE  # 0x40f810, 0x40f8b0
REMAP_RECORDS, REMAP_RECORD_SIZE, REMAP_COUNT = 0x508828, 20, 8  # {index, data pointer, name[12]}


def install(session, frames: tuple[int, ...], out: Path) -> dict:
    """Hooks the capture of `frames` (game frame counter values) into the session."""
    m = session.m
    out.mkdir(parents=True, exist_ok=True)
    state = {"armed": None, "done": []}

    def on_draw():
        f = m.r32(game.FRAME_COUNTER)
        if f in frames and f not in state["done"] and m.r16(game.GAME_MODE) == 3:
            m.write(REDRAW_FLAGS, bytes([m.read(REDRAW_FLAGS, 1)[0] | FULL_REDRAW]))
            m.write(DIRTY_GRID, bytes([1]) * DIRTY_CELLS)
            state["armed"] = f

    def on_terrain_drawn():
        if state["armed"] is not None:
            state["terrain"] = render.screen_bitmap(m)[2]

    def on_sprites_drawn():
        f = state["armed"]
        if f is None:
            return
        state["armed"] = None
        state["done"].append(f)
        write(m, out / f"f{f:05d}.cap", f, state.pop("terrain"))
        log.info("captured frame %d", f)

    session.hook(DRAW_GAME, on_draw)
    session.hook(BEFORE_SPRITES, on_terrain_drawn)
    session.hook(AFTER_SPRITES, on_sprites_drawn)
    return state


def write(m, path: Path, frame: int, terrain: bytes) -> None:
    w, h, pixels, _ = render.screen_bitmap(m)
    map_w, map_h = m.r16(MAP_W), m.r16(MAP_H)
    blobs = {"screen": pixels, "terrain": terrain}
    for name, base, count in POOLS:
        blobs["images_" + name] = m.read(base, IMAGE_SIZE * count)
    blobs["sprites"] = m.read(SPRITES, SPRITE_SIZE * SPRITE_COUNT)
    blobs["row_heads"] = m.read(ROW_HEADS, 4 * ROWS)
    blobs["row_tails"] = m.read(ROW_TAILS, 4 * ROWS)
    blobs["vgrp_health"] = m.read(VGRP_HEALTH, VGRP_SIZE * 12)
    blobs["vgrp_boxes"] = m.read(VGRP_BOXES, VGRP_SIZE * 64)
    blobs["tiles"] = m.read(m.r32(MAP_TILES_PTR), 2 * map_w * map_h)
    blobs["remap_records"] = m.read(REMAP_RECORDS, REMAP_RECORD_SIZE * REMAP_COUNT)
    header = {"frame": frame, "screen": [w, h], "camera": [m.r32(CAMERA_X), m.r32(CAMERA_Y)],
              "map": [map_w, map_h], "vision": m.read(VISION, 1)[0], "tileset": m.r16(TILESET),
              "pools": [[name, base, count] for name, base, count in POOLS], "sprites_base": SPRITES,
              "vgrp": {"health": VGRP_HEALTH, "boxes": VGRP_BOXES}, "blobs": []}
    offset = 0
    for name, data in blobs.items():
        header["blobs"].append([name, offset, len(data)])
        offset += len(data)
    with open(path, "wb") as f:
        f.write(json.dumps(header).encode() + b"\n")
        for data in blobs.values():
            f.write(data)


def read(path: Path) -> tuple[dict, dict[str, bytes]]:
    raw = Path(path).read_bytes()
    nl = raw.index(b"\n")
    header = json.loads(raw[:nl])
    body = raw[nl + 1:]
    return header, {name: body[o:o + n] for name, o, n in header["blobs"]}
