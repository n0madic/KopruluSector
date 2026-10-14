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
            (1, scenarios.RELATION_NYDUS, 2), (2, scenarios.RELATION_NYDUS, 1), (3, 0, 0), (4, scenarios.RELATION_ADDON, 3)]


if __name__ == "__main__":
    unittest.main()
