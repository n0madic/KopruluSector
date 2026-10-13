"""UNIT records and property application against the native property evidence."""
from __future__ import annotations

import json
import os
import unittest
from pathlib import Path

from converter.derive import rules, unit_records

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
EVIDENCE = ROOT / "docs/research/evidence/native-unit-properties-probes.json"
# Inert fixture state of the probe before the property call: full HP and shields.
FIXTURE_ENERGY, FIXTURE_RESOURCES = 25600, 42330


def _apply(inp: dict) -> unit_records.Applied:
    current = unit_records.Applied(inp["maximum_hp_fixed"], inp["maximum_shields"] << 8, FIXTURE_ENERGY, 0,
                                   FIXTURE_RESOURCES)
    return unit_records.apply_properties(
        inp["valid_flags"], inp["health_percent"], inp["shield_percent"], inp["energy_percent"], inp["state_flags"],
        inp["resource_value_u32"], max_hp_fixed=inp["maximum_hp_fixed"], max_shields=inp["maximum_shields"],
        max_energy=unit_records.HERO_ENERGY if inp["hero_flag"] else unit_records.NORMAL_ENERGY,
        resource_container=inp["resource_container_flag"], current=current)


class UnitRecordsTest(unittest.TestCase):
    def test_native_property_outputs(self):
        for family in ("synthetic_cases", "campaign_distinct_property_cases"):
            with self.subTest(family=family):
                cases = [c for c in json.loads(EVIDENCE.read_text())[family] if "input" in c]
                assert cases
                for c in cases:
                    got = _apply(c["input"])
                    want = c["observed"]
                    assert (got.health_fixed, got.shields_fixed, got.energy_fixed, got.status, got.resources) == (
                        want["health_fixed"], want["shields_fixed"], want["energy_fixed"], want["status"], want["resources"]), c["input"]

    def test_native_melee_filter(self):
        cases = [c for c in json.loads(EVIDENCE.read_text())["synthetic_cases"] if c.get("kind") == "UNIT_mode_filter"]
        assert len(cases) == 56
        for c in cases:
            assert unit_records.excluded_by_melee_filter(c["mode_byte"], c["owner"], c["type"]) == bool(c["skip"]), c

    def test_energy_byte_truncation(self):
        full = unit_records.Applied(0, 0, 0, 0, 0)
        kw = dict(max_hp_fixed=256, max_shields=0, resource_container=False, current=full)
        assert unit_records.apply_properties(unit_records.VALID_ENERGY, 0, 0, 128, 0, 0, max_energy=200, **kw).energy_fixed == 0
        assert unit_records.apply_properties(unit_records.VALID_ENERGY, 0, 0, 128, 0, 0, max_energy=250, **kw).energy_fixed == 64 << 8

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_campaign_inputs_derive_from_records(self):
        """The 1,215 distinct native inputs are exactly what the UNIT records, units.dat and the
        map overrides give for the eligible records (no start location, no special states, no
        positive hangar)."""
        from converter.formats import chk, dat
        from converter.mpq.profile import Resolver

        evidence = json.loads(EVIDENCE.read_text())
        want = {json.dumps(c["input"], sort_keys=True) for c in evidence["campaign_distinct_property_cases"]}
        layout = next(f for f in dat.load_layouts() if f.table == "units")
        with Resolver(ORIGINAL, full_hash=False) as r:
            units_dat = r.read("arr\\units.dat")[1]
            assoc = rules.associations(units_dat)
            hp = dat.values(layout, units_dat, "hit_points")
            shields = dat.values(layout, units_dat, "shield_amount")
            flags = dat.values(layout, units_dat, "special_ability_flags")
            derived, records = set(), 0
            for m in json.loads((ROOT / "docs/research/evidence/retail-manifest.json").read_text())["campaign_found"]:
                data = r.read(m["path"])[1]
                sections: dict[str, list[bytes]] = {}
                for c in chk.enumerate_chunks(data):
                    sections.setdefault(c.tag, []).append(data[c.payload_offset : c.payload_offset + c.size])
                overrides = [(t, sections[t][0]) for t in ("UNIS", "UNIx") if t in sections]
                e = rules.effects_of(overrides, assoc)
                for rec in unit_records.decode(sections["UNIT"][0]):
                    records += 1
                    if rec.type == unit_records.START_LOCATION or rec.states & ~unit_records.STATE_INVINCIBLE \
                            or (rec.valid_flags & unit_records.VALID_HANGAR and rec.hangar & 0xFF):
                        continue
                    derived.add(json.dumps({
                        "type": rec.type, "owner": rec.owner, "valid_flags": rec.valid_flags,
                        "health_percent": rec.hp_percent, "shield_percent": rec.shield_percent,
                        "energy_percent": rec.energy_percent, "state_flags": rec.states,
                        "maximum_hp_fixed": e.units["health"].get(rec.type, hp[rec.type]),
                        "maximum_shields": e.units["shields"].get(rec.type, shields[rec.type]),
                        "hero_flag": bool(flags[rec.type] & 0x40),
                        "resource_value_u32": rec.resources, "hangar_value_u16": rec.hangar,
                        "resource_container_flag": bool(flags[rec.type] & unit_records.RESOURCE_CONTAINER),
                    }, sort_keys=True))
        assert records == evidence["UNIT_records_audited"]
        assert derived == want

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_native_start_locations(self):
        import hashlib

        from converter.formats import chk
        from converter.mpq.profile import Resolver

        cases = json.loads(EVIDENCE.read_text())["campaign_start_cases"]
        by_record = {(c["map_path"], c["record_sha256"]): c for c in cases}
        seen = 0
        with Resolver(ORIGINAL, full_hash=False) as r:
            for path in sorted({c["map_path"] for c in cases}):
                data = r.read(path)[1]
                unit = next(data[c.payload_offset : c.payload_offset + c.size] for c in chk.enumerate_chunks(data)
                            if c.tag == "UNIT")
                for at in range(0, len(unit), 36):
                    rec = unit_records.decode(unit[at : at + 36])[0]
                    start, camera = unit_records.start_location(rec, local_player=0)
                    if start is None:
                        continue
                    c = by_record[path, hashlib.sha256(unit[at : at + 36]).hexdigest()]
                    assert (c["owner"], c["x"], c["y"]) == (rec.owner, *start)
                    assert tuple(c["camera_tile"]) == (camera or (0xFFFF, 0xFFFF)), c
                    seen += 1
        assert seen == len(cases) == 324
