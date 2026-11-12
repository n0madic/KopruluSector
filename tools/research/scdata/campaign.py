"""The campaign manifest (`defs/campaign/<profile>.json`) and its container `campaign.kpd` (`CAMP`).

The manifest keeps the executable's campaign tables (`0x50e0b8`, `0x50e0c4`) and its cheat table; the container adds
the mission directories of `arr\\mapdata.dat`/`mapdata.tbl` (the strings `0x51abb8` that ResolveNextScenarioRow
`0x4bba20` compares a next scenario's name with). Layout: docs/formats/kpd.md. native/kp_core/src/core/kp_campaign.c reads it.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass

from . import CONVERTER_VERSION
from .catalog import MAPDATA_DAT, MAPDATA_TBL
from .formats.tbl import decode_tbl
from .kpf.container import Container
from .source import Source
from .provenance import DEFS_ROOT

CAMPAIGN_KIND = b"CAMP"
CAMPAIGN_SCHEMA = 1
ROW = struct.Struct("<HHHBB")  # label, mission, cinematic, race, hidden (the executable's 8-byte row)
MISSIONS = 65  # the directory strings 0x51abb8 (LoadGameData 0x4c1400 converts 65 TBL ids)


@dataclass(frozen=True)
class Table:
    name: str
    expansion: int
    race: int
    va: int
    rows: tuple[tuple[int, int, int, int, int], ...]


@dataclass(frozen=True)
class Manifest:
    tables: tuple[Table, ...]  # pointer order: original Zerg, Terran, Protoss, then the expansion's
    tables_va: dict[str, int]
    cheats: tuple[tuple[int, int, int, int, str], ...]  # va, hash low, hash high, bit, text
    carried: int


def load(profile_id: str) -> Manifest:
    raw = json.loads((DEFS_ROOT / "campaign" / f"{profile_id}.json").read_text())
    tables = tuple(Table(t["name"], t["expansion"], t["race"], int(t["va"], 16), tuple(tuple(r) for r in t["rows"]))
                   for t in raw["tables"])
    cheats = tuple((int(va, 16), int(lo, 16), int(hi, 16), int(bit, 16), text)
                   for va, lo, hi, bit, text in raw["cheats"])
    return Manifest(tables, {k: int(v, 16) for k, v in raw["tables_va"].items()}, cheats,
                    int(raw["cheats_carried"], 16))


def directories(mapdata_dat: bytes, mapdata_tbl: bytes) -> list[bytes]:
    """The 65 strings of 0x51abb8 by mapdata index (b"" where the TBL id is 0: GetTblString returns none)."""
    strings = decode_tbl(mapdata_tbl)
    refs = struct.unpack(f"<{MISSIONS}I", mapdata_dat[:4 * MISSIONS])
    return [strings[ref - 1] if ref != 0 else b"" for ref in refs]


def tables_section(m: Manifest) -> bytes:
    """TABL: u8 count; per table u8 expansion, u8 race, u16 rows, the rows (ROW). Compiled in too (tools/defs_c.py)."""
    tabl = bytearray([len(m.tables)])
    for t in m.tables:
        tabl += struct.pack("<BBH", t.expansion, t.race, len(t.rows))
        for r in t.rows:
            tabl += ROW.pack(*r)
    return bytes(tabl)


def cheats_section(m: Manifest) -> bytes:
    """CHEA: u32 carried bits, u16 count; per cheat u32 bit, u8 length, its text. Compiled in too (tools/defs_c.py)."""
    chea = bytearray(struct.pack("<IH", m.carried, len(m.cheats)))
    for _, _, _, bit, text in m.cheats:
        raw = text.encode("ascii")
        chea += struct.pack("<IB", bit, len(raw)) + raw
    return bytes(chea)


def build(m: Manifest, dirs: list[bytes], *, profile_id: str, data_set_hash: bytes) -> Container:
    mapd = bytearray(struct.pack("<H", len(dirs)))
    for d in dirs:
        mapd += struct.pack("<H", len(d)) + d
    c = Container(CAMPAIGN_KIND, CAMPAIGN_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("TABL", tables_section(m))
    c.add("MAPD", bytes(mapd))
    c.add("CHEA", cheats_section(m))
    return c


def build_from(source: Source) -> Container:
    """`campaign.kpd` from the source's `arr\\mapdata` tables."""
    dirs = directories(source.read(MAPDATA_DAT)[1], source.read(MAPDATA_TBL)[1])
    return build(load(source.profile.id), dirs, profile_id=source.profile.id, data_set_hash=source.data_set_hash())
