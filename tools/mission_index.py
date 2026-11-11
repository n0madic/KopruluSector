"""The campaign catalog the tools number their missions by (mapdata index, name, tileset and size), read from the 1.17
install as the runtime reads it (arr\\mapdata.dat and each mission's CHK; ADR 0018).

The install is KOPRULU_ORIGINAL, else the maintainer's under koprulu-private/retail/extracted/app.
"""
from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from converter import missions  # noqa: E402
from converter.mpq.profile import Profile, Resolver  # noqa: E402

DEFAULT_INSTALL = ROOT / "koprulu-private" / "retail" / "extracted" / "app"


@dataclass(frozen=True)
class Mission:
    index: int  # the mapdata index
    name: str  # `terran/terran01`
    era: int  # the tileset's index (ERA & 7)
    width: int
    height: int


def install() -> Path | None:
    """The 1.17 install the environment names, or the maintainer's; None when there is none."""
    named = os.environ.get("KOPRULU_ORIGINAL")
    path = Path(named) if named else DEFAULT_INSTALL
    return path if (path / "StarCraft.exe").exists() else None


def catalog(data: Path) -> list[Mission]:
    """The catalog's missions in order, from the install `data`."""
    with Resolver(data, Profile.load()) as resolver:
        return [Mission(e.index, e.name, s.u16("ERA ") & 7, s.u16("DIM "), s.u16("DIM ", 1))
                for e, s in missions.catalog_scenarios(resolver)]


def require() -> Path:
    """The install, or exit saying how to name it."""
    data = install()
    if data is None:
        sys.exit("no 1.17 install: name it with KOPRULU_ORIGINAL (the campaign's catalog is read from it)")
    return data
