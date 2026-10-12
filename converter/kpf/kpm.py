"""Mission container (`.kpm`, kind `MISN`): canonical sections from a validated CHK.

Layout: docs/formats/kpm.md. The sections keep the payloads the native loader reads,
without normalisation; the runtime applies the native handlers. With the tileset's CV5/VF4,
`build` also adds the terrain-derived sections WALK, RGNS and CONT (converter/derive).
"""
from __future__ import annotations

import functools
import hashlib
import json
import logging
import struct
from dataclasses import dataclass
from typing import Callable

from .. import CONVERTER_VERSION
from ..derive import contours, regions, rules, terrain, walk
from ..formats import wav
from ..formats.chk import Scenario
from ..provenance import provenance_hash
from .container import Container
from ..errors import ConverterError

log = logging.getLogger(__name__)

KIND = b"MISN"
SCHEMA = 1

# META: version, raw era, width, height, OWNR[12], SIDE[12], FORC[20], SPRP name/description,
# COLR[8], flags, 3 pad bytes.
META = struct.Struct("<4H12s12s20s2H8sB3x")
META_COLR = 0x01

# Raw section payloads copied unchanged: (kpm fourcc, CHK tag).
RAW = (("MTXM", "MTXM"), ("MASK", "MASK"), ("SPRT", "THG2"), ("UNIT", "UNIT"), ("LOCS", "MRGN"),
       ("STRS", "STR "), ("TRIG", "TRIG"), ("MBRF", "MBRF"), ("UPRP", "UPRP"))

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
# WAVS entry: WAV string id, mission sound index, RIFF data length, nAvgBytesPerSec.
WAVS_ENTRY = struct.Struct("<HHII")
WAVS_SCHEMA = 2


class KpmError(ConverterError, ValueError):
    """The scenario cannot be represented in the mission container."""


def map_string(strs: bytes, index: int) -> bytes | None:
    """`GetMapString` 0x445670: low 16 bits, 1-based, 0 -> None, past the count -> empty string."""
    index &= 0xFFFF
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
    data_length: int = 0  # RIFF data bytes (native walker), for the transmission duration
    avg_bytes_per_sec: int = 0


def mission_sounds(scenario: Scenario, directory: str, read: Callable[[str], bytes | None],
                   rejected: list[str] | None = None) -> list[MissionSound]:
    """`read(path)` returns the effective file or None when the archives do not have it. A
    file the native WAV parser rejects plays nothing, like a missing one; its path is
    appended to `rejected`."""
    out = []
    for sid in wav_string_ids(scenario.sections):
        raw = map_string(scenario.sections["STR "], sid)
        path = f"{directory}\\{raw.decode('latin1')}" if raw else ""
        data = read(path) if path else None
        info = None
        if data is not None:
            try:
                info = wav.read_info(data)
            except wav.WavError as exc:
                log.warning("mission sound rejected by the native WAV parser, treated as missing: %s: %s", path, exc)
                if rejected is not None:
                    rejected.append(path)
        if info is None:
            out.append(MissionSound(sid, path, False))
        else:
            out.append(MissionSound(sid, path, True, info.data_length, info.avg_bytes_per_sec))
    return out


# Profile tables the mission conversion reads: the CHK loader tables, and through the PACK
# analysis (converter/analyze) the AI and IScript operand tables, the recipes and the DAT layouts.
PROVENANCE = ("chk", "ai", "iscript", "requirements", "dat")


def terrain_inputs_hash(width: int, height: int, mtxm: bytes, cv5: bytes, vf4: bytes) -> bytes:
    """Inputs hash of the terrain-derived sections: SHA-256 of MTXM, CV5, VF4, u16 width, u16 height."""
    return hashlib.sha256(mtxm + cv5 + vf4 + struct.pack("<HH", width, height)).digest()[:16]


@functools.lru_cache(maxsize=8)
def _megatile_flags(vf4: bytes) -> tuple[int, ...]:
    return tuple(terrain.megatile_flags(vf4))


def import_terrain(width: int, height: int, mtxm: bytes, cv5: bytes, vf4: bytes) -> tuple[list[int], list[int]]:
    """(megatile ids, tile flags) of `0x445720` with the tileset's cached megatile flags."""
    try:
        return terrain.import_terrain(mtxm, width, height, cv5, _megatile_flags(vf4))
    except terrain.TerrainError as exc:
        raise KpmError(f"terrain cannot be loaded: {exc}") from exc


def add_terrain_derived(c: Container, width: int, height: int, mtxm: bytes, cv5: bytes, vf4: bytes,
                        imported: tuple[list[int], list[int]] | None = None) -> None:
    """WALK, RGNS and CONT: the native terrain graph state before doodads and units (`0x490d80`).
    `imported` is `import_terrain` of the same inputs when the caller has it already."""
    ids, flags = imported or import_terrain(width, height, mtxm, cv5, vf4)
    try:
        grid = walk.walk_grid(width, height, ids, vf4)
        graph = regions.build(width, height, flags, ids, vf4, grid)
    except regions.RegionError as exc:
        raise KpmError(f"terrain cannot be loaded: {exc}") from exc
    inputs = terrain_inputs_hash(width, height, mtxm, cv5, vf4)
    for name, payload, module in (("WALK", grid, walk), ("RGNS", regions.encode(graph), regions),
                                  ("CONT", contours.encode(contours.build(width, height, grid)), contours)):
        c.add_derived(name, payload, algorithm_id=module.ALGORITHM, algorithm_version=module.VERSION,
                      inputs_hash=inputs)


PACK_ALGORITHM, PACK_VERSION = b"PAK1", 1


def add_packs(c: Container, packs: list[str], inputs: bytes) -> None:
    """PACK: u8 count, then u8 length + ASCII name per resource pack, sorted (converter/analyze)."""
    payload = bytes([len(packs)]) + b"".join(bytes([len(n)]) + n.encode("ascii") for n in sorted(packs))
    c.add_derived("PACK", payload, algorithm_id=PACK_ALGORITHM, algorithm_version=PACK_VERSION,
                  inputs_hash=hashlib.sha256(inputs).digest())


def build(scenario: Scenario, sounds: list[MissionSound], *, profile_id: str, data_set_hash: bytes,
          tileset: tuple[bytes, bytes], imported: tuple[list[int], list[int]] | None = None) -> Container:
    """Mission container; `tileset` = (CV5, VF4) of `ERA & 7` for the terrain-derived sections,
    `imported` its `import_terrain` result when the caller has it already."""
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
                  provenance_hash=provenance_hash(*PROVENANCE))
    c.add("META", meta)
    for name, tag in RAW:
        c.add(name, s[tag])
    c.add("RULE", rules.encode_rule_section(rules.rule_sections(s)))
    wavs = bytearray(struct.pack("<H", len(sounds)))
    index = 0
    for snd in sounds:
        wavs += WAVS_ENTRY.pack(snd.string_id, index if snd.exists else NO_SOUND, snd.data_length,
                                snd.avg_bytes_per_sec)
        index += snd.exists
    c.add("WAVS", bytes(wavs), schema_version=WAVS_SCHEMA)
    add_terrain_derived(c, width, height, s["MTXM"], *tileset, imported=imported)
    return c


def manifest_entry(name: str, scenario: Scenario, sounds: list[MissionSound]) -> dict:
    """Readable record for generated/reports: section sizes and the mission sound list."""
    return {
        "mission": name,
        "chk_version": scenario.version,
        "sections": {t: len(p) for t, p in scenario.sections.items()},
        "ignored": list(scenario.ignored),
        "sounds": [{"string": snd.string_id, "path": snd.path, "exists": snd.exists, "data_length": snd.data_length,
                    "avg_bytes_per_sec": snd.avg_bytes_per_sec} for snd in sounds],
    }


def dumps(entry: dict) -> str:
    return json.dumps(entry, indent=1, sort_keys=True)
