"""TRIG/MBRF record decoding for the dependency analyzer.

Record layout (2,400 bytes, docs/research/campaigns.md, atlas P17/P40): 16 conditions of
20 bytes (type byte at +0x0f), 64 actions of 32 bytes from +320 (location +0, string +4,
WAV string +8, time +0x0c, player +0x10, number +0x14, unit type +0x18, action +0x1a,
modifier +0x1b, flags +0x1c), execution flags u32 at +2368 and 28 owner bytes at +2372.
Counts are static upper bounds: disabled actions and impossible conditions are included.
"""
from __future__ import annotations

import struct
from collections import Counter
from dataclasses import dataclass

RECORD = 2400
CONDITIONS, CONDITION_SIZE = 16, 20
ACTIONS, ACTIONS_OFFSET, ACTION_SIZE = 64, 320, 32
EXECUTION_FLAGS, OWNERS = 2368, 2372

# Gameplay action ids (TRIG table 0x50df68, atlas P40).
RUN_AI_SCRIPT, RUN_AI_SCRIPT_AT = 15, 16
CREATE_UNIT_WITH_PROPERTIES, CREATE_UNIT = 11, 44
SET_NEXT_SCENARIO = 41
TRANSMISSION, PLAY_WAV, TALKING_PORTRAIT = 7, 8, 29
UNIT_CREATING_ACTIONS = frozenset({CREATE_UNIT, CREATE_UNIT_WITH_PROPERTIES})
# Briefing action ids (table 0x503908, atlas P17).
BRIEFING_SHOW_PORTRAIT, BRIEFING_TALKING_PORTRAIT, BRIEFING_TRANSMISSION = 5, 7, 8


@dataclass(frozen=True)
class Action:
    trigger: int
    index: int
    location: int
    string: int
    wav: int
    time: int
    player: int
    number: int
    unit: int
    kind: int
    modifier: int
    flags: int


@dataclass(frozen=True)
class Trigger:
    index: int
    conditions: tuple[int, ...]  # condition type bytes, zeros included
    actions: tuple[Action, ...]  # every slot with a nonzero action id
    execution_flags: int
    owners: bytes


def decode(payload: bytes) -> list[Trigger]:
    if len(payload) % RECORD:
        raise ValueError(f"trigger payload of {len(payload)} bytes is not a multiple of {RECORD}")
    out = []
    for t in range(len(payload) // RECORD):
        base = t * RECORD
        conditions = tuple(payload[base + j * CONDITION_SIZE + 0x0F] for j in range(CONDITIONS))
        actions = []
        for j in range(ACTIONS):
            a = base + ACTIONS_OFFSET + j * ACTION_SIZE
            loc, string, wav, time, player, number, unit, kind, modifier, flags = struct.unpack_from("<6IHBBB", payload, a)
            if kind:
                actions.append(Action(t, j, loc, string, wav, time, player, number, unit, kind, modifier, flags))
        out.append(Trigger(t, conditions, tuple(actions), struct.unpack_from("<I", payload, base + EXECUTION_FLAGS)[0],
                           payload[base + OWNERS : base + RECORD]))
    return out


def condition_histogram(triggers: list[Trigger]) -> Counter:
    return Counter(c for t in triggers for c in t.conditions if c)


def action_histogram(triggers: list[Trigger]) -> Counter:
    return Counter(a.kind for t in triggers for a in t.actions)


def ai_launches(triggers: list[Trigger]) -> list[Action]:
    return [a for t in triggers for a in t.actions if a.kind in (RUN_AI_SCRIPT, RUN_AI_SCRIPT_AT)]
