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

A pack whose GRP frame rectangles exceed ATLAS_AREA_BUDGET pixels is split into numbered
atlases `<pack>_1`, `<pack>_2`, ... (ADR 0003: more atlases, not pages); otherwise its one
atlas is named after the pack. Stock data needs no split (docs/formats/packs.md).
"""
from __future__ import annotations

PACKS = ("terran", "protoss", "zerg", "neutral", "effects", "doodads", "other")
SOUND_PACKS = ("terran", "protoss", "zerg", "effects")
# Frame-rectangle pixels per atlas. The budget dates from Defold's single 4096^2 atlas pages (ADR 0003); the
# largest stock packs (Zerg 11.18 MP, effects 11.01 MP) stay below it, so every pack is one atlas of grpmeta.
ATLAS_AREA_BUDGET = 11_500_000


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


def assign_atlases(grps: list[tuple[str, int]]) -> dict[str, str]:
    """Atlas name per GRP path. `grps` lists (path, frame-rectangle pixels) in images.tbl
    order; a pack over budget is cut into consecutive runs of that order."""
    by_pack: dict[str, list[tuple[str, int]]] = {}
    for path, area in grps:
        by_pack.setdefault(pack_of_grp(path), []).append((path, area))
    out = {}
    for pack, items in by_pack.items():
        if sum(area for _, area in items) <= ATLAS_AREA_BUDGET:
            out.update((path, pack) for path, _ in items)
            continue
        number, used = 1, 0
        for path, area in items:
            if used and used + area > ATLAS_AREA_BUDGET:
                number, used = number + 1, 0
            out[path] = f"{pack}_{number}"
            used += area
    return out
