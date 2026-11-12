"""The atlas order of the HD packs (converter/emit/hd.py).

The pack (`PACK`) and music (`MUSC`) containers the converter wrote went with the SD containers (ADR 0018, stage D8d): the
runtime packs the GRPs itself, and `pack_entries` is the atlas order the HD packs follow. The speech's `MSND` went in stage
C2 (ADR 0019): the runtime plays a language's lines and voices from the install or the repack's language group.
"""
from __future__ import annotations

from . import kpd


def pack_entries(gs: kpd.GrpSet, pack: str) -> list[tuple[str, list[kpd.GrpEntry]]]:
    """The pack's atlases in grpmeta ATLS order, each with its GRPS entries in order."""
    return [(name, [e for e in gs.grps if e.atlas == name]) for name, owner, _ in gs.atlases if owner == pack]
