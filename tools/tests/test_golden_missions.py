"""The full-game emulator traces of tests/golden/oracle/missions (tools/oracle/export_emulator_traces.py): the M1
reference (the scripted Wasteland victory) and the catalog Wasteland run without input load and are internally
consistent. They guard the exported data; the simulation's digest comparison is the oracle suites'."""
from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MISSIONS = ROOT / "tests" / "golden" / "oracle" / "missions"


def golden(name: str) -> dict | list:
    return json.loads((MISSIONS / f"{name}.json").read_text())


@unittest.skipUnless(MISSIONS.exists(), "no golden emulator traces (private, not in the repository)")
class GoldenMissionsTest(unittest.TestCase):
    def check_digests(self, trace: dict, frames: int) -> None:
        f = {name: i for i, name in enumerate(trace["fields"])}
        assert len(trace["digests"]) == frames
        for i, row in enumerate(trace["digests"], start=1):
            assert row[f["f"]] == i, "frame number"
            assert len(row[f["h"]]) == 16, "digest hash length"
            assert len(row[f["min"]]) == 8, "minerals per player"
            assert 0 <= row[f["rng"]] < 2**32, "rng range"

    def test_index(self) -> None:
        assert golden("index")[:2] == ["m1_wasteland", "terran01_no_input"]

    def test_m1_wasteland(self) -> None:
        m1 = golden("m1_wasteland")
        assert m1["mission"] == "terran/terran01"
        assert m1["status"] == "victory"
        assert m1["result"]["frame"] == 5617
        assert m1["result"]["value"] == 3
        self.check_digests(m1, 5663)
        assert m1["input_log"] and m1["events"]
        times = [e["t"] for e in m1["input_log"]]
        assert times == sorted(times), "input log in time order"

    def test_terran01_no_input(self) -> None:
        run = golden("terran01_no_input")
        assert run["mission"] == "terran/terran01"
        self.check_digests(run, 3000)
        assert run["sounds"]


if __name__ == "__main__":
    unittest.main()
