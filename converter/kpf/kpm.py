"""The mission helpers of the languages' overlays and the survey: the `.kpm` kind (`MISN`) a language's mission overlay
carries (emit/locale.py: STRS), the map strings, the WAV strings of
the triggers and the mission's sounds as the game finds them, and MPAK's payload (tools/defs_c.py).

The runtime builds a mission from its CHK (ADR 0018, kp_mission_from_files); the terrain state it derives has its
reference in converter/derive/mission.py. Layout of the overlays: docs/formats/kpm.md.
"""
from __future__ import annotations

import logging
import struct
from dataclasses import dataclass
from typing import Callable

from ..formats import wav
from ..formats.chk import Scenario
from ..errors import ConverterError

log = logging.getLogger(__name__)

KIND = b"MISN"
SCHEMA = 1

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


class KpmError(ConverterError, ValueError):
    """A map string runs past its section."""


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


def packs_payload(packs: list[str]) -> bytes:
    """A mission's packs as the profile's MPAK lists them (tools/defs_c.py): u8 count, then u8 length + ASCII name per
    resource pack, sorted (converter/analyze)."""
    return bytes([len(packs)]) + b"".join(bytes([len(n)]) + n.encode("ascii") for n in sorted(packs))
