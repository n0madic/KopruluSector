"""Scenario specs: the CHK UNIT records a spec's units become, and its triggers, texts and locations."""
from __future__ import annotations

import struct
import unittest

from converter import scenarios
from converter.analyze import triggers as trig


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


class TriggersTest(unittest.TestCase):
    def test_records_and_texts(self):
        spec = {"triggers": [{"owners": [0, 17], "flags": 4,
                              "conditions": [{"type": "switch", "resource": 3, "comparison": 2}],
                              "actions": [{"type": "display_text_message", "string": "hello", "flags": 4},
                                          {"type": 13, "number": 3, "modifier": 4}]}]}
        data, texts = scenarios._triggers(spec, 10)
        assert len(data) == trig.RECORD and texts == [b"hello"]
        t = trig.decode(data)[0]
        assert t.conditions[0] == trig.CONDITION_IDS["switch"] and data[0x10] == 3 and data[0x0e] == 2
        assert [(a.kind, a.string, a.flags) for a in t.actions] == [(9, 11, 4), (13, 0, 0)]
        assert t.execution_flags == 4 and t.owners[0] == 1 and t.owners[17] == 1 and sum(t.owners[:27]) == 2

    def test_bad_fields(self):
        for spec in ({"triggers": [{"owners": [27], "actions": []}]},
                     {"triggers": [{"owners": [0], "actions": [{"type": "no_such_action"}]}]},
                     {"triggers": [{"owners": [0], "conditions": [{"type": "always", "time": 1}]}]}):
            with self.assertRaises(ValueError):
                scenarios._triggers(spec, 0)

    def test_strings_appended(self):
        base = struct.pack("<HHH", 2, 6, 8) + b"a\0b\0"
        out = scenarios._strings(base, [b"cd", b"e"])
        count, *offsets = struct.unpack_from("<5H", out, 0)
        assert count == 4
        assert [out[o:out.index(b"\0", o)] for o in offsets] == [b"a", b"b", b"cd", b"e"]

    def test_locations(self):
        out = scenarios._locations(bytes(20 * 64), {"2": {"rect": [1, 2, 3, 4], "elevation": 8}})
        assert scenarios.LOCATION.unpack_from(out, 20) == (1, 2, 3, 4, 0, 8)
        with self.assertRaises(ValueError):
            scenarios._locations(bytes(20 * 64), {"65": {"rect": [0, 0, 1, 1]}})

    def test_validate(self):
        good, _ = scenarios._triggers({"triggers": [{"owners": [0], "actions": [{"type": "order", "number": 255}]}]}, 0)
        trig.validate(good)
        bad = bytearray(good)
        bad[trig.ACTIONS_OFFSET + 0x1a] = 60
        with self.assertRaises(ValueError):
            trig.validate(bytes(bad))
        bad = bytearray(good)
        struct.pack_into("<I", bad, trig.ACTIONS_OFFSET + 0x14, 256)
        with self.assertRaises(ValueError):
            trig.validate(bytes(bad))
        bad = bytearray(good)
        bad[0x0f] = 24
        with self.assertRaises(ValueError):
            trig.validate(bytes(bad))
        # entries the runners never reach stay as the map has them: ignored ones and those after the end
        unreached = bytearray(good)
        unreached[0x0f] = 24
        unreached[trig.CONDITION_FLAGS] = trig.ENTRY_IGNORED
        unreached[trig.ACTIONS_OFFSET + trig.ACTION_SIZE * 2 + 0x1a] = 60
        unreached[trig.CONDITION_SIZE * 2 + 0x0f] = 25
        trig.validate(bytes(unreached))


if __name__ == "__main__":
    unittest.main()
