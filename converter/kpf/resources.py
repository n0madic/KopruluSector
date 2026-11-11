"""Sound containers of the language overlays (docs/formats/packs.md): `MSND`, the OGG files of a mission's or a language's
sounds by index (`converter speech`, converter/emit/speech.py).

The pack (`PACK`) and music (`MUSC`) containers the converter wrote went with the SD containers (ADR 0018, stage D8d): the
runtime packs the GRPs itself, and `pack_entries` is the atlas order the HD packs follow (converter/emit/hd.py).
"""
from __future__ import annotations

import struct

from .. import CONVERTER_VERSION
from . import kpd
from .container import Container

MISSION_SOUNDS_KIND = b"MSND"
MISSION_SOUNDS_SCHEMA = 1


def sounds_section(sounds: list[tuple[int, bytes]]) -> bytes:
    """SNDS: u16 count; per sound sorted by id: u16 id, u32 size, the OGG bytes."""
    out = bytearray(struct.pack("<H", len(sounds)))
    for sid, ogg in sorted(sounds, key=lambda s: s[0]):
        out += struct.pack("<HI", sid, len(ogg)) + ogg
    return bytes(out)


def pack_entries(gs: kpd.GrpSet, pack: str) -> list[tuple[str, list[kpd.GrpEntry]]]:
    """The pack's atlases in grpmeta ATLS order, each with its GRPS entries in order."""
    return [(name, [e for e in gs.grps if e.atlas == name]) for name, owner, _ in gs.atlases if owner == pack]


def build_mission_sounds(sounds: list[tuple[int, bytes]], *, profile_id: str, data_set_hash: bytes) -> Container:
    """An MSND: SNDS keyed by the mission's WAVS sound index, or by sfx id for a language's unit voices."""
    c = Container(MISSION_SOUNDS_KIND, MISSION_SOUNDS_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("SNDS", sounds_section(sounds))
    return c
