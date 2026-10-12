"""Override effects against the native output tables of every campaign override section."""
from __future__ import annotations

import hashlib
import json
import os
import struct
from pathlib import Path

import pytest

from converter.derive import rules

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
POISON = 0xAA  # the probe's initial fill of every output region
UNIT_ARRAYS = (0x520708, 0x520540, 0x521D90, 0x5238B0, 0x523520, 0x5236E8, 0x524458)
UPGRADE_ARRAYS = (0x5248D0, 0x524950, 0x5249D0, 0x524A50, 0x524AD0, 0x524B50)
TECH_ARRAYS = (0x51F760, 0x51F7B8, 0x51F810, 0x51F868)


def _player_address(upgrade: bool, player: int, entity: int, which: int) -> int:
    """Native per-player tables: base ids in one array, expansion ids in a second one."""
    if upgrade:
        if entity < 46:
            return (0x534368 if which == 0 else 0x534590) + player * 46 + entity
        return (0x53652A if which == 0 else 0x5365DE) + player * 15 + entity
    if entity < 24:
        return (0x534104 if which == 0 else 0x534224) + player * 24 + entity
    return (0x536318 if which == 0 else 0x536408) + player * 20 + entity


def native_regions(tag: str, e: rules.Effects) -> list[tuple[int, bytes]]:
    """Serialize one section's effects into the native output regions the probe hashed."""
    writes: dict[int, int] = {}

    def put(address: int, value: int, width: int) -> None:
        for i, b in enumerate(value.to_bytes(width, "little")):
            writes[address + i] = b

    if tag == "PUNI":
        regions = [(0x52655C, 2736)]
        for (p, u), v in e.availability.items():
            put(0x52655C + p * 228 + u, v, 1)
    elif tag in ("UPGR", "PUPx", "PTEC", "PTEx"):
        upgrade = tag in ("UPGR", "PUPx")
        regions = ([(0x534368, 552), (0x534590, 552), (0x536558, 180), (0x53660C, 180)] if upgrade
                   else [(0x534104, 288), (0x534224, 288), (0x536330, 240), (0x536420, 240)])
        for (f, p, u), v in (e.upgrade_levels if upgrade else e.tech_states).items():
            put(_player_address(upgrade, p, u, f), v, 1)
    elif tag in ("UPGS", "UPGx", "TECS", "TECx"):
        upgrade = tag.startswith("UPG")
        arrays, names = (UPGRADE_ARRAYS, rules.UPGRADE_FIELDS) if upgrade else (TECH_ARRAYS, rules.TECH_FIELDS)
        regions = [(a, 122 if upgrade else 88) for a in arrays]
        for a, name in zip(arrays, names):
            for u, v in (e.upgrades if upgrade else e.techs)[name].items():
                put(a + 2 * u, v, 2)
    else:
        regions = [(a, 228 * w) for a, (_, w) in zip(UNIT_ARRAYS, rules.UNIT_FIELDS)]
        regions += [(0x525C80, 260), (0x525D88, 260)]
        for a, (name, width) in zip(UNIT_ARRAYS, rules.UNIT_FIELDS):
            for u, v in e.units[name].items():
                put(a + u * width, v, width)
        for weapon, v in e.weapon_damage.items():
            put(0x525C80 + 2 * weapon, v, 2)
        for weapon, v in e.weapon_bonus.items():
            put(0x525D88 + 2 * weapon, v, 2)
    return [(a, bytes(writes.get(a + i, POISON) for i in range(n))) for a, n in regions]


def _associations() -> list[tuple[int, int, int]]:
    return [(rules.NO_SUBUNIT, u % 130, rules.NO_WEAPON) for u in range(rules.UNITS)]


def test_puni_default_selection():
    raw = bytearray(0x1644)
    raw[0] = 7  # player 0, unit 0
    raw[12 * 228] = 9  # global unit 0
    raw[13 * 228 + 228] = 1  # player 1 uses the global value
    e = rules.effects_of([("PUNI", bytes(raw))], _associations())
    assert e.availability[0, 0] == 7 and e.availability[1, 0] == 9 and e.availability[2, 0] == 0


def test_planar_upgrades_and_expansion_gate():
    n = 61
    raw = bytearray(0x90E)
    raw[0 * 12 * n + 50] = 3  # player 0 max of upgrade 50
    raw[1 * 12 * n + 50] = 2  # player 0 start of upgrade 50
    e = rules.effects_of([("PUPx", bytes(raw))], _associations(), expansion=False)
    assert e.upgrade_levels[0, 0, 50] == 3 and (1, 0, 50) not in e.upgrade_levels
    assert rules.effects_of([("PUPx", bytes(raw))], _associations()).upgrade_levels[1, 0, 50] == 2


def test_unit_weapons_and_unsupported_unis():
    raw = bytearray(b"\1" * 228) + bytearray(0x1048 - 228)
    raw[5] = 0
    assoc = _associations()
    assoc[5] = (rules.NO_SUBUNIT, 120, rules.NO_WEAPON)
    e = rules.effects_of([("UNIx", bytes(raw))], assoc)
    assert set(e.units["health"]) == {5} and set(e.weapon_damage) == {120}
    unis = bytearray(b"\1" * 228) + bytearray(0xFD0 - 228)
    unis[5] = 0
    with pytest.raises(rules.RuleError, match="weapon 120"):
        rules.effects_of([("UNIS", bytes(unis))], assoc)


@pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
def test_campaign_sections_match_native():
    from converter.formats import chk
    from converter.mpq.profile import Resolver

    evidence = json.loads((ROOT / "docs/research/evidence/native-map-overrides-probes.json").read_text())
    with Resolver(ORIGINAL, full_hash=False) as r:
        assoc = rules.associations(r.read("arr\\units.dat")[1])
        checked = 0
        for m in evidence["campaign_input_coverage"]:
            data = r.read(m["map_path"])[1]
            sections = {c.tag: data[c.payload_offset : c.payload_offset + c.size] for c in chk.enumerate_chunks(data)}
            for s in m["sections"]:
                raw = sections[s["tag"]]
                assert hashlib.sha256(raw).hexdigest() == s["input_sha256"]
                observed = native_regions(s["tag"], rules.effects_of([(s["tag"], raw)], assoc))
                want = [(int(o["va"], 16), o["sha256"]) for o in s["output_regions"]]
                assert [(a, hashlib.sha256(b).hexdigest()) for a, b in observed] == want, (m["map_path"], s["tag"])
                checked += 1
    assert checked == evidence["campaign_section_checks"] == 396
