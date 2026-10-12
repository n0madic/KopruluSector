"""Effects of the UMS override sections (RULE) on the game tables.

Python twin of game/data/rules.lua. It follows the native handlers (atlas P23,
docs/research/native-map-overrides.md) and reproduces the native output tables of every
campaign override section (converter/tests/test_rules.py):

- UNIS/UNIx (`0x4680d0`/`0x467f40`): units 227..0 whose default flag is 0 take health,
  shields, armor, build time, mineral and gas cost and name string; then the ground and air
  weapon of the unit's weapon unit (subunit 1, or the unit itself when that is 228) take
  damage and bonus unless the weapon is 130. UNIS has 100 weapon slots; a custom unit with
  an expansion weapon would read past the native buffer and is rejected.
- UPGS/UPGx, TECS/TECx: entries with default flag 0 take their costs/times (UPGx has one
  pad byte after its 61 flags).
- PUNI: per player and unit, availability = global value if the player's default flag is
  set, else the player's value; values stay raw bytes.
- UPGR/PUPx, PTEC/PTEx (planar layout): per player and entry, (max, start) or
  (available, researched) = the global pair if the default byte is set, else the player's
  pair. The start/researched setters (`0x49a370`, `0x49a300`) ignore upgrades >= 46 and
  techs >= 24 without Brood War.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from ..errors import ConverterError

UNITS, WEAPONS, PLAYERS = 228, 130, 12
NO_WEAPON, NO_SUBUNIT = 130, 228
UNIT_FIELDS = (("health", 4), ("shields", 2), ("armor", 1), ("build_time", 2), ("mineral_cost", 2),
               ("gas_cost", 2), ("name", 2))
UPGRADE_FIELDS = ("mineral_base", "mineral_factor", "gas_base", "gas_factor", "time_base", "time_factor")
TECH_FIELDS = ("mineral_cost", "gas_cost", "research_time", "energy")
# tag -> (entity count, weapon slots or pad, section length)
LAYOUT = {
    "UNIS": (UNITS, 100, 0xFD0), "UNIx": (UNITS, WEAPONS, 0x1048),
    "UPGS": (46, 0, 0x256), "UPGx": (61, 1, 0x31A), "TECS": (24, 0, 0xD8), "TECx": (44, 0, 0x18C),
    "UPGR": (46, 0, 0x6D4), "PUPx": (61, 0, 0x90E), "PTEC": (24, 0, 0x390), "PTEx": (44, 0, 0x688),
    "PUNI": (UNITS, 0, 0x1644),
}
BW_UPGRADES, BW_TECHS = 46, 24  # first ids whose start/researched setters need Brood War


class RuleError(ConverterError, ValueError):
    """An override the original cannot apply safely (outside the supported corpus)."""


@dataclass
class Effects:
    """Values written by the overrides; absent keys keep their DAT/initial values."""
    units: dict[str, dict[int, int]] = field(default_factory=lambda: {n: {} for n, _ in UNIT_FIELDS})
    weapon_damage: dict[int, int] = field(default_factory=dict)
    weapon_bonus: dict[int, int] = field(default_factory=dict)
    upgrades: dict[str, dict[int, int]] = field(default_factory=lambda: {n: {} for n in UPGRADE_FIELDS})
    techs: dict[str, dict[int, int]] = field(default_factory=lambda: {n: {} for n in TECH_FIELDS})
    availability: dict[tuple[int, int], int] = field(default_factory=dict)  # (player, unit)
    upgrade_levels: dict[tuple[int, int, int], int] = field(default_factory=dict)  # (field 0 max / 1 start, p, id)
    tech_states: dict[tuple[int, int, int], int] = field(default_factory=dict)  # (0 available / 1 researched, p, id)


def apply(effects: Effects, tag: str, raw: bytes, associations: list[tuple[int, int, int]], *,
          expansion: bool = True) -> None:
    """Apply one section. `associations[u]` = (subunit 1, ground weapon, air weapon) from units.dat."""
    count, extra, length = LAYOUT[tag]
    if len(raw) != length:
        raise RuleError(f"{tag}: {len(raw)} bytes, expected {length}")
    if tag in ("UNIS", "UNIx"):
        offset, fields = UNITS, []
        for name, width in UNIT_FIELDS:
            fields.append((name, offset, width))
            offset += UNITS * width
        weapons = extra
        for u in range(UNITS - 1, -1, -1):
            if raw[u]:
                continue
            for name, at, width in fields:
                effects.units[name][u] = int.from_bytes(raw[at + u * width : at + (u + 1) * width], "little")
            sub = associations[u][0]
            weapon_unit = u if sub == NO_SUBUNIT else sub
            for weapon in associations[weapon_unit][1:]:
                if weapon == NO_WEAPON:
                    continue
                if weapon >= weapons:
                    raise RuleError(f"{tag}: custom unit {u} uses weapon {weapon}; the native handler reads "
                                    "past its buffer (atlas P23 0x4680d0)")
                effects.weapon_damage[weapon] = struct.unpack_from("<H", raw, offset + 2 * weapon)[0]
                effects.weapon_bonus[weapon] = struct.unpack_from("<H", raw, offset + 2 * (weapons + weapon))[0]
    elif tag in ("UPGS", "UPGx", "TECS", "TECx"):
        target, names = (effects.upgrades, UPGRADE_FIELDS) if tag.startswith("UPG") else (effects.techs, TECH_FIELDS)
        for u in range(count):
            if raw[u]:
                continue
            for f, name in enumerate(names):
                target[name][u] = struct.unpack_from("<H", raw, count + extra + (f * count + u) * 2)[0]
    elif tag == "PUNI":
        for p in range(PLAYERS):
            for u in range(UNITS):
                i = p * UNITS + u
                effects.availability[p, u] = raw[12 * UNITS + u] if raw[13 * UNITS + i] else raw[i]
    else:
        upgrade = tag in ("UPGR", "PUPx")
        target = effects.upgrade_levels if upgrade else effects.tech_states
        gated = BW_UPGRADES if upgrade else BW_TECHS
        for p in range(PLAYERS):
            for u in range(count):
                i = p * count + u
                default = raw[26 * count + i]
                # Native order: start/researched first (field 1), then max/available (field 0).
                for f in (1, 0):
                    value = raw[(24 + f) * count + u] if default else raw[f * 12 * count + i]
                    if f == 1 and u >= gated and not expansion:
                        continue
                    target[f, p, u] = value


def effects_of(rules: list[tuple[str, bytes]], associations: list[tuple[int, int, int]], *,
               expansion: bool = True) -> Effects:
    e = Effects()
    for tag, raw in rules:
        apply(e, tag, raw, associations, expansion=expansion)
    return e


def rule_sections(sections: dict[str, bytes]) -> list[tuple[str, bytes]]:
    """The override sections among a scenario's read sections (`chk.Scenario.sections`), in
    native dispatch order."""
    return [(tag, payload) for tag, payload in sections.items() if tag in LAYOUT]


def encode_rule_section(rules: list[tuple[str, bytes]]) -> bytes:
    """The .kpm RULE section: {fourcc, u32 size, payload} entries (inverse of
    `parse_rule_section`)."""
    return b"".join(tag.encode("latin1") + struct.pack("<I", len(raw)) + raw for tag, raw in rules)


def parse_rule_section(payload: bytes) -> list[tuple[str, bytes]]:
    """The .kpm RULE section: {fourcc, u32 size, payload} entries in native dispatch order."""
    out, pos = [], 0
    while pos < len(payload):
        tag, size = struct.unpack_from("<4sI", payload, pos)
        out.append((tag.decode("latin1"), payload[pos + 8 : pos + 8 + size]))
        pos += 8 + size
    return out


def associations(units_dat: bytes) -> list[tuple[int, int, int]]:
    from ..formats import dat

    layout = next(f for f in dat.load_layouts() if f.table == "units")
    sub, ground, air = (dat.values(layout, units_dat, c) for c in ("subunit1", "ground_weapon", "air_weapon"))
    return list(zip(sub, ground, air))
