#!/usr/bin/env python3
"""The original 1.17 fog of war on any tile flags and any camera of its 640 x 400 view (atlas P23, P24).

`FogMachine` maps StarCraft.exe into a bare Unicorn and runs the frame's fog chain of DrawGameScreenLayer 0x445ea0 on
inputs it is given: ResetFogGrids 0x46c050 (SampleAndSmoothFogGrid 0x46b9e0 and the copy to the last drawn grid),
BuildFogLevelMap 0x46b910 (through the thunk 0x46c560) and DrawFogOverlay 0x46c3c0 with its full-redraw flag, which
darkens the screen bitmap [0x6d0b8c] through the dark rows 0x66dd20. The inputs are what those functions read: the
map size 0x5264b4/0x5264b6, the tile flags [0x666454], the local masks 0x672668/0x67266c, the replay bytes 0x67cc90,
0x67cc84, 0x67cc88, the camera 0x543218/0x54321c with its tile 0x5264b0/0x5264b2 (x >> 5, as SetScreenPosition
0x40b450 sets it) and the pixels under the fog. The buffers InitFogOfWar 0x46c1e0 allocates are the machine's own,
zeroed, and the interpolation table is BuildFogLerpTable 0x46bfc0's.

`tools/fog_check.py` checks the machine against the full-game emulator's fog captures (the same view, levels and
pixels) and composes the original's views into a wider one (`--wide`): ADR 0009's wider views against the original.

Usage (private venv with unicorn 2.1.4 and pefile), a smoke run over one capture:
  python tools/research/probe_native_fog_117.py --exe DIR/StarCraft.exe CAPTURE.cap
"""
from __future__ import annotations

import argparse
import hashlib
import logging
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pefile  # noqa: E402
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

from probe_native_iscript_117 import TARGET_HASH  # noqa: E402

log = logging.getLogger("probe_native_fog")

IMAGE_BASE = 0x400000
VIEW_W, VIEW_H = 640, 400  # the game view DrawFogOverlay shades
SCREEN_W, SCREEN_H = 640, 480  # the screen bitmap [0x6d0b8c] it shades in (stride 640)
TILE = 32
FOG_SEEN = 0x1F  # 0x671a80, set by InitFogOfWar
DARK_SIZE = 32 * 256
GRID_SIZE, LEVELS_SIZE = 24 * 17, 88 * 60  # InitFogOfWar's 408 and 0x14a0 bytes
MAX_TILES = 256 * 256

# Native addresses (docs/research/atlas/p23.md, p24.md).
RESET_FOG_GRIDS, BUILD_LEVEL_MAP, DRAW_FOG_OVERLAY, BUILD_LERP_TABLE = 0x46C050, 0x46B910, 0x46C3C0, 0x46BFC0
SCREEN_TILE_X, SCREEN_TILE_Y, MAP_W, MAP_H = 0x5264B0, 0x5264B2, 0x5264B4, 0x5264B6
CAMERA_X, CAMERA_Y = 0x543218, 0x54321C
TILE_FLAGS_PTR = 0x666454
SMOOTH_PTR, DRAWN_PTR, RAW_PTR, LEVELS_PTR = 0x670A70, 0x670A74, 0x670A78, 0x670A7C
FOG_SEEN_LEVEL = 0x671A80
NOT_VISIBLE, NOT_EXPLORED = 0x672668, 0x67266C
REPLAY_VISION, REPLAY_REVEAL, REPLAY = 0x67CC84, 0x67CC88, 0x67CC90
DARK = 0x66DD20
SCREEN_PIXELS_PTR = 0x6D0B8C

# The machine's own memory.
STACK, STOP = 0x900000, 0xA00000
RAW, SMOOTH, DRAWN, LEVELS = 0xB00000, 0xB00200, 0xB00400, 0xB00600
SCREEN, TILES = 0xB10000, 0xC00000
INSTRUCTION_LIMIT = 20_000_000


class ProbeError(RuntimeError):
    """The original code faulted or did not return, or an input does not fit."""


class FogMachine:
    def __init__(self, exe: bytes) -> None:
        if hashlib.sha256(exe).hexdigest() != TARGET_HASH:
            raise ProbeError("StarCraft.exe is not the 1.17.0.1 target")
        pe = pefile.PE(data=exe)
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(IMAGE_BASE, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
        u.mem_write(IMAGE_BASE, pe.get_memory_mapped_image())
        for address, size in ((STACK, 0x10000), (STOP, 0x1000), (RAW, 0x10000), (SCREEN, 0x50000),
                              (TILES, 4 * MAX_TILES)):
            u.mem_map(address, size)
        for ptr, address in ((RAW_PTR, RAW), (SMOOTH_PTR, SMOOTH), (DRAWN_PTR, DRAWN), (LEVELS_PTR, LEVELS),
                             (TILE_FLAGS_PTR, TILES), (SCREEN_PIXELS_PTR, SCREEN)):
            self.w(ptr, address)
        self.w(FOG_SEEN_LEVEL, FOG_SEEN)
        self.call(BUILD_LERP_TABLE)

    def w(self, address: int, value: int, size: int = 4) -> None:
        self.u.mem_write(address, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))

    def call(self, entry: int, ecx: int = 0) -> None:
        u = self.u
        sp = STACK + 0xF000
        u.mem_write(sp, struct.pack("<I", STOP))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, ecx)
        try:
            u.emu_start(entry, STOP, count=INSTRUCTION_LIMIT)
        except UcError as e:
            raise ProbeError(f"{e} at {u.reg_read(UC_X86_REG_EIP):#x}") from e
        if u.reg_read(UC_X86_REG_EIP) != STOP:
            raise ProbeError(f"{entry:#x} did not return")

    def load(self, vision: dict, tiles: bytes, dark: bytes | None = None) -> None:
        """A map's tile flags (u32 each) and its vision as a fog capture's header holds it (`map` [W, H],
        `not_visible`, `not_explored`, `replay`, `replay_vision`, `replay_reveal`), and the tileset's dark rows; the
        grids start zeroed, as InitFogOfWar allocates them."""
        map_w, map_h = vision["map"]
        if len(tiles) != 4 * map_w * map_h or map_w * map_h > MAX_TILES:
            raise ProbeError(f"tile flags of {len(tiles)} bytes for a {map_w} x {map_h} map")
        u = self.u
        u.mem_write(TILES, tiles)
        self.w(MAP_W, map_w, 2)
        self.w(MAP_H, map_h, 2)
        self.w(NOT_VISIBLE, vision["not_visible"])
        self.w(NOT_EXPLORED, vision["not_explored"])
        self.w(REPLAY, vision["replay"], 1)
        self.w(REPLAY_VISION, vision["replay_vision"])
        self.w(REPLAY_REVEAL, vision["replay_reveal"])
        if dark is not None:
            if len(dark) != DARK_SIZE:
                raise ProbeError(f"dark rows of {len(dark)} bytes")
            u.mem_write(DARK, dark)
        for address, size in ((RAW, GRID_SIZE), (SMOOTH, GRID_SIZE), (DRAWN, GRID_SIZE), (LEVELS, LEVELS_SIZE)):
            u.mem_write(address, bytes(size))

    def view(self, cam_x: int, cam_y: int, before: bytes) -> bytes:
        """The 640 x 400 view at camera (cam_x, cam_y) (on the 8-pixel grid, as SetScreenPosition keeps it) after a
        forced fog pass over the pixels `before`."""
        if len(before) != VIEW_W * VIEW_H:
            raise ProbeError(f"a view of {len(before)} pixels")
        if cam_x & 7 or cam_y & 7 or cam_x < 0 or cam_y < 0:
            raise ProbeError(f"camera ({cam_x}, {cam_y}) off the 8-pixel grid")
        self.w(CAMERA_X, cam_x)
        self.w(CAMERA_Y, cam_y)
        self.w(SCREEN_TILE_X, cam_x >> 5, 2)
        self.w(SCREEN_TILE_Y, cam_y >> 5, 2)
        self.call(RESET_FOG_GRIDS)
        self.call(BUILD_LEVEL_MAP)
        self.u.mem_write(SCREEN, before + bytes(SCREEN_W * (SCREEN_H - VIEW_H)))
        self.call(DRAW_FOG_OVERLAY, ecx=1)
        return bytes(self.u.mem_read(SCREEN, VIEW_W * VIEW_H))

    def grids(self) -> dict[str, bytes]:
        """The buffers of the last view: the raw and blurred tile levels, the last drawn grid, the level map."""
        return {name: bytes(self.u.mem_read(address, size))
                for name, address, size in (("raw", RAW, GRID_SIZE), ("smooth", SMOOTH, GRID_SIZE),
                                            ("drawn", DRAWN, GRID_SIZE), ("levels", LEVELS, LEVELS_SIZE))}


def main() -> int:
    from sc_emulator.fogcap import FogCapture

    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("captures", nargs="+", type=Path)
    a = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    m = FogMachine(a.exe.read_bytes())
    bad = 0
    for path in a.captures:
        cap = FogCapture(path)
        h = cap.header
        m.load(h, cap["tiles"], cap["dark"])
        after = m.view(h["camera"][0], h["camera"][1], cap["before"])
        equal = after == cap["after"] if h["forced"] else None
        bad += equal is False
        result = {True: "equal", False: "DIFF", None: "partial capture"}[equal]
        log.info("%s camera %s: %s", path.name, h["camera"], result)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
