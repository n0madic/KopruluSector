"""Campaign mission enumeration from StarCraft.exe's own campaign tables (P41).

``0x50e0b8`` (original) and ``0x50e0c4`` (expansion) each hold three row pointers
(Zerg, Terran, Protoss). A row is ``{u16 label, u16 mission (arr\\mapdata.dat index),
u16 cinematic, u8 race, u8 hidden}``; mission 0 ends a table. Rows with cinematic 0 are
playable missions. The mission-warp table ``0x4eba18`` gives the cheat prefix, race,
base number and expansion flag; the warp token of a row follows ParseMissionWarpNumber
(P41 0x4bbb60): the n-th visible row is ``prefix + (base + n)``, a hidden row reached k
rows after a visible one gets the letter ``chr(ord('a') + k)``.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import pefile

from .mission import MissionSpec

CAMPAIGN_ORDER = [  # (table, race index, campaign name, cd)
    ("orig", 1, "terran", "original"),
    ("orig", 0, "zerg", "original"),
    ("orig", 2, "protoss", "original"),
    ("exp", 2, "xprotoss", "expansion"),
    ("exp", 1, "xterran", "expansion"),
    ("exp", 0, "xzerg", "expansion"),
]
TABLES = {"orig": 0x50E0B8, "exp": 0x50E0C4}
WARP_TABLE = 0x4EBA18
MISSION_WARP_FORBIDDEN = 63  # HandleMissionWarpCheat refuses mission 63 (P03 0x40e030)


@dataclass
class Row:
    label: int
    mission: int
    cinematic: int
    race: int
    hidden: int


def read_tables(exe: Path):
    pe = pefile.PE(str(exe))
    img = pe.get_memory_mapped_image()

    def r32(a):
        return struct.unpack_from("<I", img, a - 0x400000)[0]

    tables = {}
    for name, base in TABLES.items():
        for race in range(3):
            p = r32(base + 4 * race)
            rows = []
            while True:
                rows.append(Row(*struct.unpack_from("<HHHBB", img, p - 0x400000)))
                if rows[-1].mission == 0:
                    break
                p += 8
            tables[(name, race)] = rows
    warps = {}
    for i in range(6):
        p, race, base, exp = struct.unpack_from("<IIII", img, WARP_TABLE - 0x400000 + 16 * i)
        prefix = img[p - 0x400000:p - 0x400000 + 16].split(b"\0")[0].decode()
        warps[("exp" if exp else "orig", race)] = (prefix, base)
    return tables, warps


def missions(exe: Path) -> list[MissionSpec]:
    tables, warps = read_tables(exe)
    out = []
    for table, race, cname, cd in CAMPAIGN_ORDER:
        prefix, base = warps[(table, race)]
        rows = tables[(table, race)]
        n = -1
        k = 0
        for i, row in enumerate(rows):
            if row.mission == 0:
                break
            if row.cinematic == 0 and row.hidden == 0:
                n += 1
                k = 0
                token = f"{prefix}{base + n}"
            elif row.hidden:
                k += 1
                token = f"{prefix}{base + n}{chr(ord('a') + k)}"
            else:
                k = 0  # cinematic rows interrupt hidden-row chains
                continue
            if row.cinematic:
                continue
            spec = MissionSpec(name=f"{cname}-m{row.mission:02d}", warp=token, cd=cd, mapdata_index=row.mission,
                               label=f"{table} race {race} row {i}")
            if row.mission == MISSION_WARP_FORBIDDEN:
                spec.inject = (row.mission, race, 1 if table == "exp" else 0)
            out.append(spec)
    return out
