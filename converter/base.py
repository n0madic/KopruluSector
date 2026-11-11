"""The base a language's overlays are measured against (`converter locale` and `speech`, ADR 0016), computed from the
source in memory: the string, interface, font and sound containers the runtime builds from the original files
(converter/gamedata.py), and each catalog mission's map strings, triggers, briefing and spoken lines."""
from __future__ import annotations

from dataclasses import dataclass

from . import gamedata, missions
from .kpf.container import Container
from .source import Source


@dataclass(frozen=True)
class Mission:
    strs: bytes  # the CHK's STR
    trig: bytes
    mbrf: bytes
    # each WAV string's path in string-id order, "" for a file that does not exist: the position is the sound's index
    # (kp_mission.c WAVS), whatever the other files of the install it was read from
    sounds: tuple[str, ...]


@dataclass(frozen=True)
class Base:
    profile_id: str
    source_hash: bytes  # every overlay container carries it
    strings: Container
    ui: Container
    fonts: Container
    sounds: Container
    missions: dict[str, Mission]  # by mission name (`terran/terran01`), in catalog order


def load(source: Source) -> Base:
    return Base(source.profile.id, source.data_set_hash(), gamedata.strings(source), gamedata.ui(source),
                gamedata.fonts(source), gamedata.sounds(source),
                {m.entry.name: Mission(m.scenario.sections["STR "], m.scenario.sections["TRIG"],
                                       m.scenario.sections["MBRF"],
                                       tuple(s.path if s.exists else "" for s in m.sounds))
                 for m in missions.load_catalog(source)})
