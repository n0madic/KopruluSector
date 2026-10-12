"""Preplaced UNIT records and their property application (`ApplyUnitRecordProperties` 0x46a420).

Python twin of game/data/units_records.lua; checked against the native outputs of all
1,215 distinct campaign property inputs and 211 synthetic boundary cases
(docs/research/native-unit-properties.md, converter/tests/test_unit_records.py).

Record (36 bytes, plan §4.2 UNIT): +0 serial, +4 x, +6 y, +8 type, +0x0a relation flags,
+0x0c valid special states (u16), +0x0e valid properties (u16), +0x10 owner, +0x11 HP %,
+0x12 shield %, +0x13 energy %, +0x14 resources (u32), +0x18 hangar (u16), +0x1a requested
states (u16), +0x1c unused, +0x20 related serial.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

RECORD = struct.Struct("<IHHHHHHBBBBIHHII")
assert RECORD.size == 36

VALID_HP = 0x00020000
VALID_SHIELDS = 0x00040000
VALID_ENERGY = 0x00080000
VALID_RESOURCES = 0x00100000
VALID_HANGAR = 0x00200000
STATE_INVINCIBLE = 0x10
STATUS_INVINCIBLE = 0x04000000
RESOURCE_CONTAINER = 0x2000  # units.dat special ability flag
MINERAL_TYPES = (176, 177, 178)
START_LOCATION = 214
HERO_ENERGY, NORMAL_ENERGY = 250, 200


@dataclass(frozen=True)
class UnitRecord:
    serial: int
    x: int
    y: int
    type: int
    relation_flags: int
    valid_states: int
    valid_properties: int
    owner: int
    hp_percent: int
    shield_percent: int
    energy_percent: int
    resources: int  # raw u32; the native store keeps the low u16
    hangar: int  # raw u16; the native store keeps the low byte
    states: int
    unused: int
    related_serial: int

    @property
    def valid_flags(self) -> int:
        """The u32 the native routine reads: special states low, properties high."""
        return self.valid_states | self.valid_properties << 16


def decode(payload: bytes) -> list[UnitRecord]:
    if len(payload) % RECORD.size:
        raise ValueError(f"UNIT payload of {len(payload)} bytes is not a multiple of 36")
    return [UnitRecord(*RECORD.unpack_from(payload, at)) for at in range(0, len(payload), RECORD.size)]


@dataclass(frozen=True)
class Applied:
    health_fixed: int
    shields_fixed: int
    energy_fixed: int
    status: int
    resources: int


def mineral_animation(resources: int) -> int:
    """`0x4247b0`: mineral field animation for a resource amount."""
    if resources < 250:
        return 13
    if resources < 500:
        return 14
    if resources < 750:
        return 15
    return 20


def apply_properties(valid_flags: int, hp_percent: int, shield_percent: int, energy_percent: int, states: int,
                     resources: int, *, max_hp_fixed: int, max_shields: int, max_energy: int,
                     resource_container: bool, current: Applied) -> Applied:
    """Fields written by `0x46a420`; fields whose validity bit is absent keep `current`."""
    health, shields, energy, status, res = (current.health_fixed, current.shields_fixed, current.energy_fixed,
                                            current.status, current.resources)
    if valid_flags & VALID_HP:
        value = ((hp_percent * max_hp_fixed) & 0xFFFFFFFF) // 100
        health = min(max(value, 1), max_hp_fixed)  # minimum one raw unit, setter 0x41bb50 caps
    if valid_flags & VALID_SHIELDS:
        shields = min(max_shields * shield_percent // 100, max_shields) << 8
    if valid_flags & VALID_ENERGY:
        energy = min(max_energy, (max_energy * energy_percent // 100) & 0xFF) << 8
    if valid_flags & STATE_INVINCIBLE and states & STATE_INVINCIBLE:
        status |= STATUS_INVINCIBLE
    if valid_flags & VALID_RESOURCES and resource_container:
        res = resources & 0xFFFF
    return Applied(health, shields, energy, status, res)


NEUTRAL_OWNER = 11
MELEE_KEPT_NEUTRAL = frozenset({176, 177, 178, 188, 89, 90, 93, 94, 95, 96})  # resources and critters


def excluded_by_melee_filter(mode_byte: int, owner: int, unit_type: int) -> bool:
    """`0x469600`: with melee mode byte 0x672949 set, keep only neutral resources/critters."""
    if not mode_byte:
        return False
    return owner != NEUTRAL_OWNER or unit_type not in MELEE_KEPT_NEUTRAL


def start_location(record: UnitRecord, local_player: int | None) -> tuple[tuple[int, int] | None, tuple[int, int] | None]:
    """`HandleStartLocationRecord` 0x46a280 outside replays: ((x, y) for buffer[owner], or
    None when the record is not a start location; camera tile set for the local player)."""
    if record.type != START_LOCATION:
        return None, None
    camera = None
    if record.owner == local_player:
        camera = (max(0, (record.x >> 5) - 10), max(0, (record.y >> 5) - 6))
    return (record.x, record.y), camera
