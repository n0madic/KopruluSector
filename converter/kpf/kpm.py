"""Mission container (`.kpm`, kind `MISN`): canonical sections from a validated CHK.

Layout: docs/formats/kpm.md. The sections keep the payloads the native loader reads,
without normalisation; the runtime applies the native handlers. Derived sections
(WALK, RGNS, CONT, PACK) are added by their producers.
"""
from __future__ import annotations

import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .. import CONVERTER_VERSION
from ..formats.chk import DEFS_DIR, Scenario
from .container import Container

KIND = b"MISN"
SCHEMA = 1

# META: version, raw era, width, height, OWNR[12], SIDE[12], FORC[20], SPRP name/description,
# COLR[8], flags, 3 pad bytes.
META = struct.Struct("<4H12s12s20s2H8sB3x")
META_COLR = 0x01

# Raw section payloads copied unchanged: (kpm fourcc, CHK tag).
RAW = (("MTXM", "MTXM"), ("MASK", "MASK"), ("SPRT", "THG2"), ("UNIT", "UNIT"), ("LOCS", "MRGN"),
       ("STRS", "STR "), ("TRIG", "TRIG"), ("MBRF", "MBRF"), ("UPRP", "UPRP"))
RULE_TAGS = ("UNIS", "UPGS", "TECS", "PUNI", "UPGR", "PTEC", "UNIx", "UPGx", "TECx", "PUPx", "PTEx")

TRIGGER_SIZE = 2400
ACTIONS_OFFSET = 320
ACTION_SIZE = 32
ACTION_COUNT = 64
ACTION_WAV = 0x08
ACTION_ID = 0x1A
# Actions that play the WAV string at +0x08: TRIG 7 Transmission, 8 Play WAV (atlas P40);
# MBRF 2 Play WAV, 8 Transmission (atlas P17).
TRIG_WAV_ACTIONS = frozenset({7, 8})
MBRF_WAV_ACTIONS = frozenset({2, 8})
NO_SOUND = 0xFFFF


class KpmError(ValueError):
    """The scenario cannot be represented in the mission container."""


def map_string(strs: bytes, index: int) -> bytes | None:
    """`GetMapString` 0x445670: 1-based, 0 -> None, past the count -> empty string."""
    if index == 0:
        return None
    count = struct.unpack_from("<H", strs, 0)[0]
    if index - 1 >= count:
        return b""
    offset = struct.unpack_from("<H", strs, 2 * index)[0]
    end = strs.find(b"\0", offset)
    if end < 0:
        raise KpmError(f"map string {index} is not terminated inside STR")
    return strs[offset:end]


def wav_string_ids(sections: dict[str, bytes]) -> list[int]:
    """Distinct WAV string ids used by TRIG and MBRF actions (disabled actions included)."""
    ids = set()
    for tag, wav_actions in (("TRIG", TRIG_WAV_ACTIONS), ("MBRF", MBRF_WAV_ACTIONS)):
        data = sections.get(tag, b"")
        for base in range(0, len(data), TRIGGER_SIZE):
            for a in range(ACTION_COUNT):
                act = base + ACTIONS_OFFSET + a * ACTION_SIZE
                if data[act + ACTION_ID] in wav_actions:
                    wav = struct.unpack_from("<I", data, act + ACTION_WAV)[0] & 0xFFFF
                    if wav:
                        ids.add(wav)
    return sorted(ids)


@dataclass(frozen=True)
class MissionSound:
    string_id: int
    path: str  # MPQ path: <mission directory>\<string>, as the native WAV actions build it
    exists: bool


def mission_sounds(scenario: Scenario, directory: str, exists: Callable[[str], bool]) -> list[MissionSound]:
    out = []
    for sid in wav_string_ids(scenario.sections):
        raw = map_string(scenario.sections["STR "], sid)
        path = f"{directory}\\{raw.decode('latin1')}" if raw else ""
        out.append(MissionSound(sid, path, bool(path) and exists(path)))
    return out


def provenance_hash() -> bytes:
    """Hash of the profile tables the mission conversion depends on."""
    digest = hashlib.sha256()
    for path in sorted(DEFS_DIR.glob("*.json")):
        digest.update(path.name.encode() + b"\0" + path.read_bytes())
    return digest.digest()


def build(scenario: Scenario, sounds: list[MissionSound], *, profile_id: str, data_set_hash: bytes) -> Container:
    s = scenario.sections
    width, height = scenario.u16("DIM "), scenario.u16("DIM ", 1)
    if len(s["MTXM"]) != 2 * width * height or len(s["MASK"]) != width * height:
        # The native loader accepts other lengths; no campaign map has one (all 64 match).
        raise KpmError(f"MTXM/MASK sizes {len(s['MTXM'])}/{len(s['MASK'])} do not match {width}x{height}")
    colr = s.get("COLR")
    meta = META.pack(
        scenario.version, scenario.u16("ERA "), width, height, s["OWNR"], s["SIDE"],
        s["FORC"].ljust(20, b"\0"),  # native FORC zero-fills the tail
        scenario.u16("SPRP"), scenario.u16("SPRP", 1), colr or bytes(8), META_COLR if colr else 0,
    )
    c = Container(KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash,
                  provenance_hash=provenance_hash())
    c.add("META", meta)
    for name, tag in RAW:
        c.add(name, s[tag])
    rule = bytearray()
    for tag in (t for t in s if t in RULE_TAGS):  # native dispatch order
        rule += tag.encode("latin1") + struct.pack("<I", len(s[tag])) + s[tag]
    c.add("RULE", bytes(rule))
    wavs = bytearray(struct.pack("<H", len(sounds)))
    index = 0
    for snd in sounds:
        wavs += struct.pack("<HH", snd.string_id, index if snd.exists else NO_SOUND)
        index += snd.exists
    c.add("WAVS", bytes(wavs))
    return c


def manifest_entry(name: str, scenario: Scenario, sounds: list[MissionSound]) -> dict:
    """Readable record for generated/reports: section sizes and the mission sound list."""
    return {
        "mission": name,
        "chk_version": scenario.version,
        "sections": {t: len(p) for t, p in scenario.sections.items()},
        "ignored": list(scenario.ignored),
        "sounds": [{"string": snd.string_id, "path": snd.path, "exists": snd.exists} for snd in sounds],
    }


def dumps(entry: dict) -> str:
    return json.dumps(entry, indent=1, sort_keys=True)
