#!/usr/bin/env python3
"""Draw health, shield, energy and timed-life bars with the original 1.17 code.

DrawHealthBar 0x464d20 (draw method 11) runs in Unicorn on authored CUnit records against
the retail units.dat and sprites.dat columns and game\\thpbar.pcx (0x66cb5c), into a guarded
indexed bitmap: every unit type with a health bar, at several hit point, shield, energy and
timer levels, owned by the local player or not, hallucinated or not, with the whole frame
and with cropped frames. The maximum energy comes from the original 0x434940 (no upgrades).
The bitmap area around the bar is folded into the 32-bit hash of
tests/c/test_present.c (health_bars_equal_original), which draws the same cases with
native/kp_present/kp_health_bar.c.

Output: --golden tests/golden/oracle/health_bar/cases.json (inputs and hashes only, no retail
bytes): per unit type [unit type, max energy (0x434940), cases, hash of the case hashes].

Usage (private venv with unicorn 2.1.4 and pefile):
  python tools/research/probe_native_health_bar_117.py --data DIR --golden OUT.json
"""
from __future__ import annotations

import argparse
import hashlib
import logging
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pefile  # noqa: E402
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

from converter.formats import dat, pcx  # noqa: E402
from converter.mpq.profile import Profile, Resolver  # noqa: E402
from probe_native_iscript_117 import MASK, TARGET_HASH, fold  # noqa: E402

sys.path.insert(0, str(ROOT / "tools" / "oracle"))
from oracle_common import golden_json  # noqa: E402

log = logging.getLogger("probe_native_health_bar")

DRAW_BAR, MAX_ENERGY = 0x464D20, 0x434940
THPBAR, BITMAP_PTR, LOCAL_PLAYER = 0x66CB5C, 0x6D0C40, 0x508D50
UNIT_TYPES = 228
SPRITE_HEALTH_BAR_FIRST = 130  # sprites.dat health bar column starts at sprite 130

# Scenario: keep equal to tests/c/test_present.c (health_bars_equal_original).
WIDTH, HEIGHT = 160, 18  # bitmap: the widest bar (150) and the tallest (13) with margins
X, Y = 4, 3  # where the visible crop's top-left is drawn
FIXED = 256
LOCAL = 0
BASELINE_MUL, BASELINE_MOD = 7, 251  # bitmap byte i = i * 7 % 251 before every case
HP_LEVELS = (1, 33, 34, 50, 66, 67, 100)  # percent of the maximum hit points (rounded down, at least 1)
SHIELD_LEVELS = (0, 50, 100)
ENERGY_LEVELS = (0, 40, 100)
HALLUCINATION, SPELLCASTER = 0x40000000, 0x200000
BROODLING = 40
HALLUCINATION_TIMER, BROODLING_TIMER = 1350, 1800
# Crops: left, top, then the visible width and height relative to the frame's (None: the rest
# after left/top).
CROPS = ((0, 0, None, None), (1, 0, None, None), (2, 0, None, None), (4, 0, None, None), (0, 0, -5, None),
         (0, 1, None, None), (0, 0, None, -6))

STACK, STOP, UNIT, SPRITE, RECT, BITMAP, PIXELS = 0x900000, 0xA00000, 0xB00000, 0xB00400, 0xB00800, 0xB00900, 0xB01000
INSTRUCTION_LIMIT = 1_000_000


def states(t: int, hit_points: int, shield_amount: int, shield_enabled: int, flags: int,
           max_energy: int) -> list[tuple[int, int, int, int, int, int]]:
    """(hp, shields, energy, timer, status, owner) of the cases of unit type t."""
    out = [(max(1, hit_points * pct // 100), shield_amount * FIXED, max_energy, 0, 0, LOCAL) for pct in HP_LEVELS]
    if shield_enabled:
        out += [(hit_points // 2, shield_amount * pct // 100 * FIXED, 0, 0, 0, LOCAL) for pct in SHIELD_LEVELS]
    if flags & SPELLCASTER:
        for pct in ENERGY_LEVELS:
            out += [(hit_points, 0, max_energy * pct // 100, 0, 0, owner) for owner in (LOCAL, LOCAL + 1)]
    out.append((hit_points, 0, 0, HALLUCINATION_TIMER // 3, HALLUCINATION, LOCAL))
    if t == BROODLING:
        out.append((hit_points, 0, 0, BROODLING_TIMER // 2, 0, LOCAL))
    return out


def crops(width: int, height: int) -> list[tuple[int, int, int, int]]:
    out = []
    for left, top, dw, dh in CROPS:
        w = width + dw if dw is not None else width - left
        h = height + dh if dh is not None else height - top
        if w > 0 and h > 0:
            out.append((left, top, w, h))
    return out


class ProbeError(RuntimeError):
    """The probe setup does not match the expected inputs."""


class Machine:
    def __init__(self, exe: bytes, files: dict[str, bytes], thpbar: bytes) -> None:
        if hashlib.sha256(exe).hexdigest() != TARGET_HASH:
            raise ProbeError("StarCraft.exe is not the 1.17.0.1 target")
        pe = pefile.PE(data=exe)
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
        u.mem_write(0x400000, pe.get_memory_mapped_image())
        for address, size in ((STACK, 0x10000), (STOP, 0x1000), (UNIT, 0x10000)):
            u.mem_map(address, size)
        for layout in dat.load_layouts():
            data = files[layout.path]
            dat.check(layout, data)
            for c in layout.columns:
                u.mem_write(c.destination_va, data[c.offset:c.offset + c.size])
        u.mem_write(THPBAR, thpbar)
        self.w(BITMAP_PTR, BITMAP)
        self.w(BITMAP, WIDTH, 2)
        self.w(BITMAP + 2, HEIGHT, 2)
        self.w(BITMAP + 4, PIXELS)
        self.w(LOCAL_PLAYER, LOCAL)
        self.baseline = bytes(i * BASELINE_MUL % BASELINE_MOD for i in range(WIDTH * HEIGHT))

    def w(self, address: int, value: int, size: int = 4) -> None:
        self.u.mem_write(address, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))

    def call(self, entry: int, ecx: int = 0, edx: int = 0, args: tuple[int, ...] = ()) -> int:
        u = self.u
        sp = STACK + 0xF000
        u.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), STOP, *(a & MASK for a in args)))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, ecx & MASK)
        u.reg_write(UC_X86_REG_EDX, edx & MASK)
        try:
            u.emu_start(entry, STOP, count=INSTRUCTION_LIMIT)
        except UcError as e:
            raise ProbeError(f"{e} at {u.reg_read(UC_X86_REG_EIP):#x}") from e
        if u.reg_read(UC_X86_REG_EIP) != STOP:
            raise ProbeError("no return")
        return u.reg_read(UC_X86_REG_EAX)

    def unit(self, unit_type: int, sprite_id: int, hp: int, shields: int, energy: int, timer: int, status: int,
             owner: int) -> None:
        self.u.mem_write(UNIT, bytes(0x150))
        self.w(UNIT + 0x8, hp)
        self.w(UNIT + 0xC, SPRITE)
        self.w(SPRITE + 0x8, sprite_id, 2)
        self.w(UNIT + 0x4C, owner, 1)
        self.w(UNIT + 0x60, shields)
        self.w(UNIT + 0x64, unit_type, 2)
        self.w(UNIT + 0xA2, energy, 2)
        self.w(UNIT + 0xDC, status)
        self.w(UNIT + 0x110, timer, 2)

    def draw(self, crop: tuple[int, int, int, int]) -> int:
        """Hash of the bitmap after drawing the bar with `crop` at (X, Y)."""
        self.u.mem_write(PIXELS, self.baseline)
        self.u.mem_write(RECT, struct.pack("<4i", *crop))
        self.call(DRAW_BAR, X, Y, (0, RECT, UNIT))
        h = 0
        for v in self.u.mem_read(PIXELS, WIDTH * HEIGHT):
            h = fold(h, v)
        return h


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data", type=Path, required=True, help="retail directory (StarCraft.exe and archives)")
    p.add_argument("--golden", type=Path, required=True)
    p.add_argument("--type", type=int, help="log the case hashes of this unit type")
    a = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    exe = (a.data / "StarCraft.exe").read_bytes()
    with Resolver(a.data, Profile.load(), full_hash=False) as resolver:
        files = {f.path: resolver.read(f.path)[1] for f in dat.load_layouts()}
        thpbar = pcx.decode(resolver.read("game\\thpbar.pcx")[1]).pixels
    m = Machine(exe, files, thpbar)
    tables = {l.table: l for l in dat.load_layouts()}
    units, sprites, flingy = tables["units"], tables["sprites"], tables["flingy"]
    graphics = dat.values(units, files[units.path], "graphics")
    hit_points = dat.values(units, files[units.path], "hit_points")
    shield_amount = dat.values(units, files[units.path], "shield_amount")
    shield_enabled = dat.values(units, files[units.path], "shield_enabled")
    flags = dat.values(units, files[units.path], "special_ability_flags")
    flingy_sprite = dat.values(flingy, files[flingy.path], "sprite")
    health_bar = dat.values(sprites, files[sprites.path], "health_bar")
    rows = []
    for t in range(UNIT_TYPES):
        sprite_id = flingy_sprite[graphics[t]]
        if sprite_id < SPRITE_HEALTH_BAR_FIRST:
            continue
        width = health_bar[sprite_id - SPRITE_HEALTH_BAR_FIRST]
        height = 13 if shield_enabled[t] else 11
        if width == 0 or width + X > WIDTH:
            continue
        m.unit(t, sprite_id, 0, 0, 0, 0, 0, LOCAL)
        max_energy = m.call(MAX_ENERGY, UNIT) & 0xFFFF
        h, n = 0, 0
        for hp, shields, energy, timer, status, owner in states(t, hit_points[t], shield_amount[t],
                                                                 shield_enabled[t], flags[t], max_energy):
            m.unit(t, sprite_id, hp, shields, energy, timer, status, owner)
            for crop in crops(width, height):
                case = m.draw(crop)
                if a.type == t:
                    log.info("hp %d shields %d energy %d timer %d status %#x owner %d crop %s: %d", hp, shields,
                             energy, timer, status, owner, crop, case)
                h, n = fold(h, case), n + 1
        rows.append((t, max_energy, n, h))
    a.golden.parent.mkdir(parents=True, exist_ok=True)
    a.golden.write_text(golden_json({
        "width": WIDTH, "height": HEIGHT, "x": X, "y": Y, "local_player": LOCAL, "baseline_mul": BASELINE_MUL,
        "baseline_mod": BASELINE_MOD, "target_sha256": TARGET_HASH, "types": [list(r) for r in rows]}))
    log.info("%d unit types, %d cases", len(rows), sum(r[2] for r in rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
