"""The OGG file names of the sounds the converter still encodes (`converter speech`, converter/emit/speech.py).

The pack, music and mission sound containers this module wrote went with the SD containers (ADR 0018, stage D8d): the
runtime packs the GRPs and reads the sounds from the original files, and `converter repack` stores them as Ogg Vorbis.
"""
from __future__ import annotations


def ogg_name(mpq_path: str, prefix: str) -> str:
    """'sound\\Terran\\MARINE\\TMaDth00.wav' -> 'terran_marine_tmadth00.ogg' (prefix removed)."""
    stem = mpq_path.lower().removeprefix(prefix).removesuffix(".wav")
    return stem.replace("\\", "_") + ".ogg"


def mission_ogg_names(mission: str, paths: list[str]) -> dict[str, str]:
    """OGG file name per WAV path of mission `mission` (`terran/terran01`): the WAV's file
    name, or its path below the mission directory when two different files of the mission
    share that file name. Paths equal but for case are one archive file and share a name."""
    prefix = "campaign\\" + mission.replace("/", "\\") + "\\"
    files: dict[str, set[str]] = {}
    for path in paths:
        files.setdefault(path.rsplit("\\", 1)[-1].lower(), set()).add(path.lower())
    names = {}
    for path in paths:
        lower = path.lower()
        unique = len(files[lower.rsplit("\\", 1)[-1]]) == 1
        names[path] = ogg_name(lower.rsplit("\\", 1)[-1] if unique else lower.removeprefix(prefix), "")
    return names
