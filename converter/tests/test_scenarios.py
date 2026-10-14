"""Scenario specs: the CHK UNIT records a spec's units become."""
from __future__ import annotations

import unittest

from converter import scenarios


def _records(units: list[dict]) -> list[tuple]:
    data = scenarios._units({"units": units})
    size = scenarios.UNIT_RECORD.size
    return [scenarios.UNIT_RECORD.unpack_from(data, at) for at in range(0, len(data), size)]


class UnitRecordsTest(unittest.TestCase):
    def test_related_units(self):
        nydus = {"type": scenarios.NYDUS_CANAL, "x": 64, "y": 64, "owner": 0}
        records = _records([
            {**nydus, "related": 1},
            {**nydus, "related": 0},
            {"type": 106, "x": 160, "y": 176, "owner": 0},
            {"type": 107, "x": 256, "y": 192, "owner": 0, "related": 2},
        ])
        # serial, relation flags and the related unit's serial (1-based, like the serials)
        assert [(r[0], r[4], r[-1]) for r in records] == [
            (1, scenarios.RELATION_NYDUS, 2), (2, scenarios.RELATION_NYDUS, 1), (3, scenarios.RELATION_ADDON, 4), (4, scenarios.RELATION_ADDON, 3)]

    def test_percentages_and_hangar(self):
        records = _records([
            {"type": 72, "x": 64, "y": 64, "owner": 0, "hp": 10, "energy": 50, "hangar": 4},
            {"type": 0, "x": 96, "y": 64, "owner": 1},
        ])
        # valid properties, hp, shields, energy, resources and hangar of each record
        assert [r[6:7] + r[8:13] for r in records] == [(0x2 | 0x8 | 0x20, 10, 100, 50, 0, 4), (0, 100, 100, 100, 0, 0)]

    def test_bad_relations(self):
        unit = {"type": 106, "x": 64, "y": 64, "owner": 0}
        for units in ([{**unit, "related": 1}], [{**unit, "related": 0}],
                      [{**unit, "related": 2}, {**unit, "related": 2}, unit]):
            with self.assertRaises(ValueError):
                _records(units)


class ResearchedTest(unittest.TestCase):
    def test_tech_bytes(self):
        techs = scenarios.TECH_SECTIONS["PTEC"]
        slots = scenarios.PLAYER_SLOTS
        payload = bytes([0] * (2 * slots * techs + 2 * techs) + [1] * (slots * techs))
        out = scenarios._researched(payload, techs, {"1": [3, techs + 5]})
        changed = [k for k in range(len(out)) if out[k] != payload[k]]
        # available, researched and "use the defaults" of slot 1's tech 3; a tech past the section is left out
        assert changed == [techs + 3, slots * techs + techs + 3, 2 * slots * techs + 2 * techs + techs + 3]
        assert [out[k] for k in changed] == [1, 1, 0]

    def test_bad_slot(self):
        with self.assertRaises(ValueError):
            scenarios._researched(bytes(1000), 24, {"12": [0]})


if __name__ == "__main__":
    unittest.main()
