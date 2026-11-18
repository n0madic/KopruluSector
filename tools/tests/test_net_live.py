"""tools/net_live.py: the mixed lobby's comparison of the starts (phase N, stage N3.5), on synthetic starts: the same
start passes; a seed, a template, a header byte, a slot record or a download that differs is reported; the game ID and
the records past the eighth between seats of different roles are not compared."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import net_live  # noqa: E402

MAP = bytes(range(256)) * 4
SEED = 0x5F3759DF


def header(game_id: int) -> str:
    h = bytearray(0x8D)
    h[0:4] = game_id.to_bytes(4, "little")
    h[4:9] = b"Alpha"
    h[net_live.TEMPLATE_AT] = 2  # the template's type: melee
    return h.hex()


def records(host: bool) -> list[dict]:
    out = [{"player": 0, "storm": 0, "type": 2, "race": 6, "force": 0, "name": "Alpha"},
           {"player": 1, "storm": 1, "type": 2, "race": 6, "force": 0, "name": "Beta"}]
    out += [{"player": k, "storm": -1, "type": 6, "race": 6, "force": 0, "name": ""} for k in range(2, 8)]
    out += [{"player": k if host else 0, "storm": -1 if host else 0, "type": 0, "race": 0, "force": 0, "name": ""}
            for k in range(8, 12)]
    return out


def emulated(game_id: int, host: bool) -> dict:
    return {"seed": SEED, "template": bytes.fromhex(header(0))[net_live.TEMPLATE_AT:][:32].hex(),
            "game_header": header(game_id), "players": records(host)}


def koprulu(role: str) -> dict:
    return {"role": role, "seed": SEED, "game_header": header(1 if role == "join" else 0), "players": records(role == "host"),
            "download_size": len(MAP) if role == "join" else 0,
            "download_crc": net_live.transfer_crc(MAP) if role == "join" else 0}


class CompareTest(unittest.TestCase):
    def test_the_same_start_passes(self):
        starts = {"Alpha": emulated(0, True), "Gamma": emulated(2, False)}
        self.assertEqual(net_live.compare_starts(koprulu("join"), starts, {"Alpha": "host", "Gamma": "join"}, MAP,
                                                 {"Gamma": None}), [])

    def test_a_joiners_game_id_and_listed_players_are_its_own(self):
        start = emulated(2, False)
        h = bytearray.fromhex(start["game_header"])
        h[0x24] = 2  # it joined when the list showed two players
        start["game_header"] = h.hex()
        self.assertEqual(net_live.compare_starts(koprulu("join"), {"Gamma": start}, {"Gamma": "join"}, MAP, {}), [])

    def test_the_transfer_crc_is_zlibs_without_the_final_inversion(self):
        self.assertEqual(net_live.transfer_crc(b"123456789"), 0xCBF43926 ^ 0xFFFFFFFF)

    def test_a_host_compares_only_the_shared_records_with_a_joiner(self):
        diffs = net_live.compare_starts(koprulu("host"), {"Alpha": emulated(1, False)}, {"Alpha": "join"}, MAP,
                                        {"Alpha": MAP})
        self.assertEqual(diffs, [])

    def test_each_difference_is_reported(self):
        start = emulated(0, True)
        start["seed"] ^= 1
        start["template"] = "00" * 32
        h = bytearray.fromhex(start["game_header"])
        h[0x20] ^= 0xFF
        start["game_header"] = h.hex()
        start["players"][1] = dict(start["players"][1], race=1)
        start["players"][9] = dict(start["players"][9], storm=3)
        kp = koprulu("join")
        kp["download_crc"] ^= 1
        diffs = net_live.compare_starts(kp, {"Alpha": start}, {"Alpha": "host"}, MAP, {"Alpha": MAP[:-1]})
        self.assertEqual(len(diffs), 6, diffs)  # record 9 is a host's only: a joiner's leaves it zero
        self.assertTrue(any("seed" in d for d in diffs))
        self.assertTrue(any("template" in d for d in diffs))
        self.assertTrue(any("+0x20" in d for d in diffs))
        self.assertTrue(any("player record 1" in d for d in diffs))
        self.assertTrue(any("Koprulu's download" in d for d in diffs))
        self.assertTrue(any("its download" in d for d in diffs))

    def test_a_start_without_a_seed_is_reported(self):
        start = emulated(0, True)
        del start["seed"]
        diffs = net_live.compare_starts(koprulu("join"), {"Alpha": start}, {"Alpha": "host"}, MAP, {})
        self.assertEqual(diffs, ["Alpha: no seed recorded (net.py's start snapshot)"])


class RecordingTest(unittest.TestCase):
    def test_the_emulated_starts_and_downloads_are_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            spec = {"players": [{"name": "Beta", "role": "external", "has_map": True},
                                {"name": "Alpha", "role": "join", "has_map": False},
                                {"name": "Gamma", "role": "join", "has_map": True}]}
            (game / "spec.json").write_text(json.dumps(spec))
            (game / "Alpha" / "files").mkdir(parents=True)
            (game / "Alpha" / "setup.json").write_text(json.dumps({"start_game": emulated(1, False)}))
            (game / "Alpha" / "files" / "test.scm").write_bytes(MAP)
            starts, roles, downloads, missing = net_live.emulated_starts(game, "test.scm")
        self.assertEqual(list(starts), ["Alpha"])
        self.assertEqual(roles, {"Alpha": "join"})
        self.assertEqual(downloads, {"Alpha": MAP})
        self.assertEqual(missing, ["Gamma"])


if __name__ == "__main__":
    unittest.main()
