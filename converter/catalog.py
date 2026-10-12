"""Campaign map catalog: `arr\\mapdata.dat` records index `arr\\mapdata.tbl` strings.

Record 0 is `null`; the others name campaign directories, and the scenario is
`<directory>\\staredit\\scenario.chk` inside the campaign archives (docs/research/campaigns.md).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .formats.tbl import decode_tbl

MAPDATA_DAT = "arr\\mapdata.dat"
MAPDATA_TBL = "arr\\mapdata.tbl"
SCENARIO_SUFFIX = "\\staredit\\scenario.chk"
NULL_ENTRY = b"null"

# Physically present scenarios outside the catalog (prerelease VER=57, rejected natively).
EXTRA_SCENARIOS = (
    "campaign\\protoss\\tutorial",
    "campaign\\terran\\terran07",
    "campaign\\terran\\terran10",
    "campaign\\zerg\\tutorial",
)


@dataclass(frozen=True)
class CatalogEntry:
    index: int  # mapdata.dat record index
    directory: str  # e.g. campaign\terran\terran01

    @property
    def chk_path(self) -> str:
        return self.directory + SCENARIO_SUFFIX

    @property
    def name(self) -> str:
        """Short id used for generated files, e.g. `terran/terran01`."""
        return "/".join(self.directory.split("\\")[1:]).lower()


def decode_catalog(mapdata_dat: bytes, mapdata_tbl: bytes) -> list[CatalogEntry]:
    strings = decode_tbl(mapdata_tbl)
    refs = struct.unpack(f"<{len(mapdata_dat) // 4}I", mapdata_dat)
    out = []
    for index, ref in enumerate(refs):
        raw = strings[ref - 1]
        if raw != NULL_ENTRY:
            out.append(CatalogEntry(index, raw.decode("ascii")))
    return out
