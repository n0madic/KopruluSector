"""Simulation state dumps for the phase 3 acceptance (plan §7 phase 3, docs/phase3.md).

At the entry of the k-th call of the object step StepObjects 0x445070 in a game (k = 1 is
the call of PrimeGameLoop 0x445520: the state the game start left, before any unit, order
or AI step; the frame counter 0x52651c is recorded in the header), the dump stores:
- the whole writable data section of StarCraft.exe (.data/.bss, 0x501000..0x6dea60): every
  pool (units, sprites, images, orders, paths), list head, counter, player record and the
  RNG live there, so a decoder reads any global at its native address;
- the heap blocks that globals point to: the tile flags (visibility, explored, creep, one
  DWORD per tile, [0x666454]) and the megatiles ([0x54323c], one WORD per tile).
File: <out>/s<k>.state = JSON header line, then the zlib-compressed blobs it lists
(name, base address, offset, compressed size, size). tools/native_state.py decodes them.
"""
from __future__ import annotations

import json
import logging
import zlib
from pathlib import Path

from . import game

log = logging.getLogger(__name__)

DATA_BASE, DATA_SIZE = 0x501000, 0x1DDA60
MAP_W, MAP_H = 0x5264B4, 0x5264B6
TILE_FLAGS_PTR = 0x666454
MEGATILES_PTR = 0x54323C


def install(session, steps: tuple[int, ...], out: Path) -> dict:
    """Hooks the dumps at the entries of the object steps numbered `steps` (1-based)."""
    m = session.m
    out.mkdir(parents=True, exist_ok=True)
    state = {"steps": 0}

    def on_step():
        if m.r16(game.GAME_MODE) != 3:
            return
        state["steps"] += 1
        k = state["steps"]
        if k in steps:
            write(m, out / f"s{k:05d}.state", m.r32(game.FRAME_COUNTER))
            log.info("state before object step %d dumped", k)

    session.hook(game.STEP_OBJECTS, on_step)
    return state


def write(m, path: Path, frame: int) -> None:
    map_w, map_h = m.r16(MAP_W), m.r16(MAP_H)
    tiles = map_w * map_h
    blobs = [("data", DATA_BASE, m.read(DATA_BASE, DATA_SIZE))]
    for name, ptr, size in (("tile_flags", TILE_FLAGS_PTR, 4 * tiles), ("megatiles", MEGATILES_PTR, 2 * tiles)):
        base = m.r32(ptr)
        blobs.append((name, base, m.read(base, size)))
    header = {"frame": frame, "map": [map_w, map_h], "rng": m.r32(game.RNG_STATE),
              "initial_seed": m.r32(game.RNG_INITIAL_SEED), "mission": m.r16(game.MISSION_INDEX), "blobs": []}
    body, offset = [], 0
    for name, base, data in blobs:
        packed = zlib.compress(data, 6)
        header["blobs"].append([name, base, offset, len(packed), len(data)])
        body.append(packed)
        offset += len(packed)
    with open(path, "wb") as f:
        f.write(json.dumps(header).encode() + b"\n")
        for packed in body:
            f.write(packed)


class State:
    """A decoded dump: read globals by native address."""

    def __init__(self, path: Path):
        raw = Path(path).read_bytes()
        nl = raw.index(b"\n")
        self.header = json.loads(raw[:nl])
        body = raw[nl + 1:]
        self.blobs = {}
        for name, base, offset, packed, size in self.header["blobs"]:
            data = zlib.decompress(body[offset:offset + packed])
            assert len(data) == size, name
            self.blobs[name] = (base, data)

    def read(self, addr: int, size: int) -> bytes:
        base, data = self.blobs["data"]
        off = addr - base
        if not 0 <= off <= len(data) - size:
            raise ValueError(f"{addr:#x}+{size} is outside the dumped data section")
        return data[off:off + size]

    def blob(self, name: str) -> bytes:
        return self.blobs[name][1]
