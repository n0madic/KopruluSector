"""Resource packs (ADR 0004): which pack holds a GRP, a sound or a portrait.

Packs are collection factories loaded per mission from its `PACK` section. Grouping by
the archive path keeps each pack's atlas within one 4096^2 page (ADR 0003):

| Pack | Content |
|---|---|
| terran, protoss, zerg | `unit\\<race>\\` GRPs, their sounds and portraits |
| neutral | `unit\\neutral\\` GRPs (critters, resources, special buildings) |
| effects | `unit\\thingy\\` (except tileset doodads) and `unit\\bullet\\` GRPs |
| doodads | `unit\\thingy\\tileset\\` GRPs (map doodads of every tileset) |
| other | anything else (none in the stock images.dat) |
"""
from __future__ import annotations

PACKS = ("terran", "protoss", "zerg", "neutral", "effects", "doodads", "other")


def pack_of_grp(path: str) -> str:
    p = path.lower()
    for race in ("terran", "protoss", "zerg", "neutral"):
        if p.startswith(f"unit\\{race}\\"):
            return race
    if p.startswith("unit\\thingy\\tileset\\"):
        return "doodads"
    if p.startswith(("unit\\thingy\\", "unit\\bullet\\")):
        return "effects"
    return "other"


def pack_of_sound(path: str) -> str:
    """`sound\\<race>\\...` -> that race; every other sound is shared (`effects`)."""
    p = path.lower()
    for race in ("terran", "protoss", "zerg"):
        if p.startswith(f"sound\\{race}\\"):
            return race
    return "effects"
