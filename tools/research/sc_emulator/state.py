"""Simulation state dumps for the phase 3 acceptance (plan §7 phase 3, docs/phase3.md).

At the entry of the k-th call of the object step StepObjects 0x445070 in a game (k = 1 is
the call of PrimeGameLoop 0x445520: the state the game start left, before any unit, order
or AI step; the frame counter 0x52651c is recorded in the header), the dump stores:
- the whole writable data section of StarCraft.exe (.data/.bss, 0x501000..0x6dea60): every
  pool (units, sprites, images, orders), list head, counter, player record and the
  RNG live there, so a decoder reads any global at its native address;
- the heap blocks that globals point to: the tile flags (visibility, explored, creep, one
  DWORD per tile, [0x666454]), the display megatiles ([0x54323c], one WORD per tile), the
  MTXM tile words the creep engine rewrites ([0x666458]), and the creep engine's backup
  words ([0x6ca7a4]) and border bytes ([0x6ca7a8]);
- the path pool ([0x69b574]: 1024 records of 128 bytes, CreatePathPool 0x48f000);
- the trigger nodes of the eight player lists (headers 0x50e0d8, atlas P41), in list order: per node its
  player byte and its 2400-byte record (the raw TRIG record with its execution flags and continuation).
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
MTXM_PTR = 0x666458
CREEP_BACKUP_PTR, CREEP_BORDER_PTR = 0x6CA7A4, 0x6CA7A8
PATH_POOL_PTR, PATH_POOL_SIZE = 0x69B574, 0x20000
DENSITY_PTR, DENSITY_SIZE = 0x67CF60, 0x7239  # the air repulsion grid, 0xab x 0xab cells
# The region object of CreateRegions 0x490d80 (SMemAlloc 0x97a20 bytes): count +0, split end +8,
# raster +0xc (256 words per row), splits +0x2000c, regions +0x449fc (64 bytes each).
REGIONS_PTR, REGIONS_SIZE = 0x69B584, 0x97A20
# The AI region arrays of InitAiRegions 0x4b0640 (one pointer per player, `count` records of 0x34 bytes after
# FinishAiMapSetup 0x48dd80 shrinks them; 2500 before).
AI_REGIONS_PTR, AI_REGION_SIZE, AI_REGIONS_MAX = 0x6B1A74, 0x34, 2500
# The player trigger lists: 12-byte Storm list headers (+8 the first node, <= 0 none); a node's +4 is the next node
# (<= 0 ends) and +8 its 2400-byte record.
TRIGGER_LISTS, TRIGGER_RECORD = 0x50E0D8, 2400


def trigger_nodes(m, player: int) -> list[int]:
    out, node = [], m.r32(TRIGGER_LISTS + 12 * player + 8)
    while 0 < node < 0x80000000:
        out.append(node)
        node = m.r32(node + 4)
    return out


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
    for name, ptr, size in (("tile_flags", TILE_FLAGS_PTR, 4 * tiles), ("megatiles", MEGATILES_PTR, 2 * tiles),
                            ("mtxm", MTXM_PTR, 2 * tiles), ("creep_backup", CREEP_BACKUP_PTR, 2 * tiles),
                            ("creep_border", CREEP_BORDER_PTR, tiles)):
        base = m.r32(ptr)
        blobs.append((name, base, m.read(base, size)))
    base = m.r32(PATH_POOL_PTR)
    blobs.append(("paths", base, m.read(base, PATH_POOL_SIZE)))
    base = m.r32(DENSITY_PTR)
    blobs.append(("density", base, m.read(base, DENSITY_SIZE)))
    base = m.r32(REGIONS_PTR)
    if base:
        blobs.append(("regions", base, m.read(base, REGIONS_SIZE)))
        count = m.r16(base)
        for p in range(8):
            ai = m.r32(AI_REGIONS_PTR + 4 * p)
            if ai:
                blobs.append((f"ai_regions{p}", ai, m.read(ai, AI_REGION_SIZE * min(count, AI_REGIONS_MAX))))
    nodes = b"".join(bytes([p]) + m.read(node + 8, TRIGGER_RECORD) for p in range(8) for node in trigger_nodes(m, p))
    blobs.append(("triggers", 0, nodes))
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

    def blob_base(self, name: str) -> int:
        return self.blobs[name][0]
