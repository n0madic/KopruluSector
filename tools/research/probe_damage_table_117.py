"""The damage table of original code (phase 4 oracle, docs/phase4.md): WeaponDamage 0x41dfc0 of every weapon at
every unit type, with the target owner's upgrades at level 0 and at level 3, on the real DAT of a game state.

The executable is mapped into Unicorn and its writable data section is replaced with the one a full-game state dump
stores (sc_emulator/state.py: DAT columns, upgrade levels, pools as the game loaded them). Each case builds a target
CUnit in scratch memory (player 0, incomplete so no damage overlay is drawn, hit points 0x40000000 so it never dies,
full shields), calls WeaponDamage(target, damage of the weapon << 8, weapon, divisor 1, direction 0, no attacker,
player 1) and records the hit points and shields it lost. The shield hit image (AddShieldHitOverlay 0x422fe0) is
skipped: it only draws.

Output: a JSON golden (tests/golden/oracle/damage_table.json) with the CRC-32 of each (level, unit type) row, the row
being "hp_loss/shield_loss" of weapons 0..129 joined by spaces; --rows writes the rows themselves for diagnosis.

Usage (from the repository root, private venv):
  python tools/research/probe_damage_table_117.py --exe .../StarCraft.exe --state .../s00001.state --out FILE
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sc_emulator.state import State, DATA_BASE  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "oracle"))
from oracle_common import golden_json  # noqa: E402

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
WEAPON_DAMAGE = 0x41DFC0
SHIELD_HIT_IMAGE = 0x422FE0  # AddShieldHitOverlay: ECX = unit, stack direction; ret 4
UNIT, SPRITE, STACK, STOP = 0x800000, 0x806000, 0x900000, 0xA00000
UNIT_TYPES, WEAPONS, UPGRADES_SC, UPGRADES_BW = 228, 130, 46, 15
# DAT columns and the upgrade levels in the data section (atlas P03, P17)
SHIELD_ENABLED, MAX_SHIELDS = 0x520458, 0x520540     # units.dat: u8, u16
WEAPON_DAMAGE_AMOUNT = 0x525C80                    # weapons.dat: u16
UPGRADE_LEVELS_SC, UPGRADE_LEVELS_BW = 0x534590, 0x5365DE  # + 46 * p + id; + 15 * p + id (id >= 46)
HP = 0x40000000
LEVELS = (0, 3)
TARGET_PLAYER, ATTACKING_PLAYER = 0, 1


def crc32(text: str) -> int:
    return zlib.crc32(text.encode()) & 0xFFFFFFFF


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--state", type=Path, required=True, help="a state dump of an expansion game (BW DAT)")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--rows", type=Path, help="also write the rows (JSON) for diagnosis")
    a = p.parse_args()
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH:
        p.error("unknown executable hash")
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
    st = State(a.state)
    if st.read(0x536720, 1)[0] != 1:
        p.error("the state is not an expansion game")
    data = st.blob("data") if hasattr(st, "blob") else st.read(DATA_BASE, 0x1DDA60)

    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x400000, size)
    uc.mem_write(0x400000, image)
    uc.mem_write(DATA_BASE, bytes(data))
    for base in (UNIT, STACK, STOP):
        uc.mem_map(base, 0x10000)

    def put(addr: int, value: int, n: int = 4) -> None:
        uc.mem_write(addr, (value & ((1 << (8 * n)) - 1)).to_bytes(n, "little"))

    def get(addr: int, n: int = 4) -> int:
        return int.from_bytes(uc.mem_read(addr, n), "little")

    def hook(m, addr, width, _):
        if addr == STOP:
            m.emu_stop()
        elif addr == SHIELD_HIT_IMAGE:
            sp = m.reg_read(UC_X86_REG_ESP)
            ret = int.from_bytes(m.mem_read(sp, 4), "little")
            m.reg_write(UC_X86_REG_ESP, sp + 8)
            m.reg_write(UC_X86_REG_EIP, ret)

    uc.hook_add(UC_HOOK_CODE, hook)

    def set_levels(level: int) -> None:
        for k in range(UPGRADES_SC):
            put(UPGRADE_LEVELS_SC + 46 * TARGET_PLAYER + k, level, 1)
        for k in range(UPGRADES_BW):
            put(UPGRADE_LEVELS_BW + 15 * TARGET_PLAYER + 46 + k, level, 1)

    def case(t: int, w: int) -> str:
        uc.mem_write(UNIT, bytes(0x150))
        uc.mem_write(SPRITE, bytes(0x40))
        shields = get(MAX_SHIELDS + 2 * t, 2) * 256 if get(SHIELD_ENABLED + t, 1) else 0
        put(UNIT + 8, HP)
        put(UNIT + 0xC, SPRITE)
        put(UNIT + 0x4C, TARGET_PLAYER, 1)
        put(UNIT + 0x60, shields)
        put(UNIT + 0x64, t, 2)
        damage = get(WEAPON_DAMAGE_AMOUNT + 2 * w, 2) << 8
        sp = STACK + 0x8000
        uc.mem_write(sp, struct.pack("<7I", STOP, damage, w, 1, 0, 0, ATTACKING_PLAYER))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, UNIT)
        uc.emu_start(WEAPON_DAMAGE, STOP + 1, count=100000)
        if uc.reg_read(UC_X86_REG_EIP) != STOP:
            raise RuntimeError(f"type {t} weapon {w}: WeaponDamage did not return")
        return f"{HP - get(UNIT + 8)}/{shields - get(UNIT + 0x60)}"

    rows, golden = {}, {"levels": list(LEVELS), "rows": {}}
    for level in LEVELS:
        set_levels(level)
        crcs = []
        for t in range(UNIT_TYPES):
            row = " ".join(case(t, w) for w in range(WEAPONS))
            rows[f"{level}/{t}"] = row
            crcs.append(crc32(row))
        golden["rows"][level] = crcs
    a.out.write_text(golden_json(golden))
    if a.rows:
        a.rows.write_text(json.dumps(rows, indent=0))
    print(f"wrote {a.out}: {len(LEVELS)} levels x {UNIT_TYPES} unit types x {WEAPONS} weapons")


if __name__ == "__main__":
    main()
