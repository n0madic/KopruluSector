"""Fog-of-war captures for plan phase 8 item 5 (docs/phase8.md, atlas P23/P24).

DrawGameScreenLayer 0x445ea0 draws the terrain and the sprites, then DrawFogOverlay 0x46c3c0 darkens the 640x400 game
view through the level map its fog helpers built this frame. In each capture frame the capture stores, at the call
of 0x46c3c0 (0x445f18) and after it returns (0x445f1d):
- the 640x400 game view before and after the fog pass (the screen bitmap 0x6d0b88, its first 400 rows);
- the full-redraw flag the pass ran with (0x6d0bf5 bit 0) and the dirty grid it walked (0x6d06d0, 40 x 30 cells of
  16x16 pixels);
- the camera (0x543218/0x54321c), the map size, the local masks (0x672668 not visible, 0x67266c not explored), the
  replay bytes (0x67cc90, 0x67cc84, 0x67cc88);
- the fog buffers: raw levels [0x670a78], blurred [0x670a70], last drawn [0x670a74] (24 x 17 each) and the level map
  [0x670a7c] (88 x 60), the interpolation table 0x670a80 (32 x 32 DWORDs) and the darkening rows 0x66dd20 (32 x 256);
- the tile flags [0x666454] (W*H DWORDs) the levels came from.
With `forced`, the frame is drawn in full (as capture.py does); otherwise the game's own partial redraw runs and the
capture shows the dirty path 0x46c3e1.
The minimap's fog (ComposeMinimapFog*, the composer [0x671fc4] of UpdateMinimapImage 0x46e140, run by the minimap's
1000 ms timer): the first composition at or after each capture frame stores, after the composer returns (0x46e188),
the minimap geometry (0x671fa8..0x671fbc), the terrain colors [0x671fcc] and the composed image [0x671fd4] (W x H),
the four fog rows 0x672178 and the unexplored color byte 0x6d2377, with the tile flags and masks of that moment.
File: <out>/fog<frame>.cap and <out>/minimap<frame>.cap = JSON header line, then the blobs it lists (name, offset,
size).
The redraws of a whole run (`install_redraws`, <out>/redraws.cap): one such record per call of 0x46c3c0, full or
partial, appended as the run goes: the full-redraw flag it ran with (ECX), the frame, the camera, the map and the
masks, the dirty cells of the view (40 x 25), the level map, the tile flags (XOR with the previous record's) and the
number of pixels the pass changed outside the dirty cells; the blobs zlib-compressed. A partial redraw leaves the cells
it was not given as an earlier redraw shaded them, while Koprulu shades the whole view every frame: tools/fog_check.py
checks that the fog of those cells is still the current level map's.
"""
from __future__ import annotations

import json
import logging
import struct
import zlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX

from . import game, render
from .machine import Machine
from .session import Session

log = logging.getLogger(__name__)

DRAW_GAME, FOG_CALL, FOG_RETURN = 0x445EA0, 0x445F18, 0x445F1D
MINIMAP_COMPOSED = 0x46E188  # in UpdateMinimapImage 0x46e140, past `call [0x671fc4]`
REDRAW_FLAGS, FULL_REDRAW = 0x6D0BF5, 0x01
DIRTY_GRID, DIRTY_COLS, DIRTY_CELL = 0x6D06D0, 40, 16
DIRTY_CELLS, VIEW_CELLS = DIRTY_COLS * 30, DIRTY_COLS * 25  # the screen's cells and the 640 x 400 view's
CAMERA_X, CAMERA_Y = 0x543218, 0x54321C
MAP_W, MAP_H = 0x5264B4, 0x5264B6
TILE_FLAGS_PTR = 0x666454
NOT_VISIBLE, NOT_EXPLORED = 0x672668, 0x67266C
REPLAY, REPLAY_VISION, REPLAY_REVEAL = 0x67CC90, 0x67CC84, 0x67CC88
LEVELS_PTR, LEVELS_SIZE = 0x670A7C, 0x14A0  # 88 x 60
GRIDS = (("raw", 0x670A78, 408), ("smooth", 0x670A70, 408), ("drawn", 0x670A74, 408), ("levels", LEVELS_PTR, LEVELS_SIZE))
LERP, LERP_SIZE = 0x670A80, 32 * 32 * 4  # the table itself (BuildFogLerpTable 0x46bfc0), up to 0x671a80
DARK, DARK_SIZE = 0x66DD20, 32 * 256
VIEW_W, VIEW_H = 640, 400
REDRAWS = "redraws.cap"
MINIMAP_GEOMETRY = 0x671FA8  # divisor, (0x671fac), shift, pixels per minimap pixel, W, H: six WORD/DWORD fields
MINIMAP_TERRAIN, MINIMAP_IMAGE = 0x671FC8, 0x671FD0  # {WORD w, WORD h, DWORD pixels}
MINIMAP_FOG_ROWS, MINIMAP_UNEXPLORED = 0x672178, 0x6D2377


def _record(header: dict, blobs: list[tuple[str, bytes]]) -> bytes:
    header["blobs"], offset = [], 0
    for name, data in blobs:
        header["blobs"].append([name, offset, len(data)])
        offset += len(data)
    return json.dumps(header).encode() + b"\n" + b"".join(data for _, data in blobs)


def _write(path: Path, header: dict, blobs: list[tuple[str, bytes]]) -> None:
    path.write_bytes(_record(header, blobs))


def _view(m: Machine) -> bytes:
    w, _, pixels, _ = render.screen_bitmap(m)
    return b"".join(pixels[y * w:y * w + VIEW_W] for y in range(VIEW_H))


def _xor(a: bytes, b: bytes) -> bytes:
    return (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(len(a), "little")


def _outside_dirty(before: bytes, after: bytes, dirty: bytes) -> int:
    """The pixels a fog pass changed outside the dirty cells it was given."""
    n = 0
    for y in range(VIEW_H):
        a, b = after[y * VIEW_W:(y + 1) * VIEW_W], before[y * VIEW_W:(y + 1) * VIEW_W]
        if a == b:
            continue
        cells = dirty[y // DIRTY_CELL * DIRTY_COLS:(y // DIRTY_CELL + 1) * DIRTY_COLS]
        for x in range(0, VIEW_W, DIRTY_CELL):
            if not cells[x // DIRTY_CELL] and a[x:x + DIRTY_CELL] != b[x:x + DIRTY_CELL]:
                n += sum(p != q for p, q in zip(a[x:x + DIRTY_CELL], b[x:x + DIRTY_CELL]))
    return n


def _state(m: Machine) -> tuple[dict, list[tuple[str, bytes]]]:
    w, h = m.r16(MAP_W), m.r16(MAP_H)
    header = {"camera": [m.r32(CAMERA_X), m.r32(CAMERA_Y)], "map": [w, h], "not_visible": m.r32(NOT_VISIBLE),
              "not_explored": m.r32(NOT_EXPLORED), "replay": m.read(REPLAY, 1)[0],
              "replay_vision": m.r32(REPLAY_VISION), "replay_reveal": m.r32(REPLAY_REVEAL)}
    return header, [("tiles", m.read(m.r32(TILE_FLAGS_PTR), 4 * w * h))]


def install(session: Session, frames: tuple[int, ...], out: Path, forced: bool = True) -> dict:
    m = session.m
    out.mkdir(parents=True, exist_ok=True)
    st = {"armed": None, "done": set(), "minimap": [], "before": None}

    def on_draw() -> None:
        f = m.r32(game.FRAME_COUNTER)
        if f in frames and f not in st["done"] and m.r16(game.GAME_MODE) == 3:
            st["armed"] = f
            if forced:
                m.write(REDRAW_FLAGS, bytes([m.read(REDRAW_FLAGS, 1)[0] | FULL_REDRAW]))
                m.write(DIRTY_GRID, bytes([1]) * DIRTY_CELLS)

    def on_fog_call() -> None:
        if st["armed"] is not None:
            st["before"] = (_view(m), m.read(REDRAW_FLAGS, 1)[0] & FULL_REDRAW, m.read(DIRTY_GRID, DIRTY_CELLS))

    def on_fog_return() -> None:
        f = st["armed"]
        if f is None or st["before"] is None:
            return
        st["armed"] = None
        st["done"].add(f)
        before, full, dirty = st.pop("before")
        st["before"] = None
        header, blobs = _state(m)
        header.update(frame=f, full=full, forced=forced)
        blobs += [("before", before), ("after", _view(m)), ("dirty", dirty), ("dark", m.read(DARK, DARK_SIZE))]
        blobs += [(name, m.read(m.r32(ptr), size)) for name, ptr, size in GRIDS] + [("lerp", m.read(LERP, LERP_SIZE))]
        _write(out / f"fog{f:05d}.cap", header, blobs)
        st["minimap"].append(f)
        log.info("fog captured at frame %d", f)

    def on_minimap() -> None:
        if not st["minimap"] or m.r16(game.GAME_MODE) != 3:
            return
        f = st["minimap"].pop(0)
        header, blobs = _state(m)
        geometry = struct.unpack("<HHHHII", m.read(MINIMAP_GEOMETRY, 16))
        header.update(frame=f, at_frame=m.r32(game.FRAME_COUNTER), geometry=list(geometry))
        for name, rec in (("terrain", MINIMAP_TERRAIN), ("image", MINIMAP_IMAGE)):
            w, h, ptr = struct.unpack("<HHI", m.read(rec, 8))
            blobs.append((name, m.read(ptr, w * h) if ptr else b""))
            header[f"{name}_size"] = [w, h]
        blobs += [("fog_rows", m.read(MINIMAP_FOG_ROWS, 4 * 256)), ("unexplored", m.read(MINIMAP_UNEXPLORED, 1))]
        _write(out / f"minimap{f:05d}.cap", header, blobs)
        log.info("minimap fog captured at frame %d (composed at %d)", f, header["at_frame"])

    session.hook(DRAW_GAME, on_draw)
    session.hook(FOG_CALL, on_fog_call)
    session.hook(FOG_RETURN, on_fog_return)
    session.hook(MINIMAP_COMPOSED, on_minimap)
    return st


def install_redraws(session: Session, out: Path) -> dict:
    """Every fog pass of the game view from here on into out/redraws.cap (the module's docstring)."""
    m = session.m
    out.mkdir(parents=True, exist_ok=True)
    path = out / REDRAWS
    path.unlink(missing_ok=True)
    st = {"call": None, "tiles": b"", "records": 0}

    def on_fog_call() -> None:
        if m.r16(game.GAME_MODE) == 3:
            st["call"] = (_view(m), m.reg(UC_X86_REG_ECX) & 0xFF, m.read(DIRTY_GRID, VIEW_CELLS))

    def on_fog_return() -> None:
        if st["call"] is None:
            return
        before, full, dirty = st["call"]
        st["call"] = None
        header, blobs = _state(m)
        tiles = blobs[0][1]
        header.update(frame=m.r32(game.FRAME_COUNTER), full=full,
                      outside=_outside_dirty(before, _view(m), dirty) if not full else 0)
        with open(path, "ab") as f:
            f.write(redraw_record(header, dirty, m.read(m.r32(LEVELS_PTR), LEVELS_SIZE), tiles, st["tiles"]))
        st["tiles"] = tiles
        st["records"] += 1
        if st["records"] % 1000 == 0:
            log.info("%d fog redraws recorded (frame %d)", st["records"], header["frame"])

    session.hook(FOG_CALL, on_fog_call)
    session.hook(FOG_RETURN, on_fog_return)
    return st


def redraw_record(header: dict, dirty: bytes, levels: bytes, tiles: bytes, previous_tiles: bytes) -> bytes:
    """One record of redraws.cap: the blobs zlib-compressed, the tile flags XOR the previous record's (whole when the
    map's size changed)."""
    header["tiles_xor"] = len(tiles) == len(previous_tiles)
    delta = _xor(tiles, previous_tiles) if header["tiles_xor"] else tiles
    blobs = (("dirty", dirty), ("levels", levels), ("tiles", delta))
    return _record(header, [(name, zlib.compress(data)) for name, data in blobs])


def redraws(path: Path):
    """The records of a redraws.cap in order: (header, blobs), the tile flags whole again."""
    tiles = b""
    with open(path, "rb") as f:
        while line := f.readline():
            header = json.loads(line)
            body = f.read(sum(size for _, _, size in header["blobs"]))
            blobs = {name: zlib.decompress(body[off:off + size]) for name, off, size in header["blobs"]}
            if header["tiles_xor"]:
                blobs["tiles"] = _xor(blobs["tiles"], tiles)
            tiles = blobs["tiles"]
            yield header, blobs


class FogCapture:
    """A capture file read back: header fields and blobs by name."""

    def __init__(self, path: Path) -> None:
        raw = Path(path).read_bytes()
        nl = raw.index(b"\n")
        self.header = json.loads(raw[:nl])
        body = raw[nl + 1:]
        self.blobs = {name: body[off:off + size] for name, off, size in self.header["blobs"]}

    def __getitem__(self, name: str) -> bytes:
        return self.blobs[name]
