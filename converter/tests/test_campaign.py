"""The campaign manifest (defs/campaign) against the executable and its evidence, and campaign.kpd."""
from __future__ import annotations

import json
import os
import struct
import unittest
from pathlib import Path

from converter import campaign
from converter.kpf.container import parse

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
PROFILE = "bw-1.17.0.1"
SELECTABLE = 57  # the rows a player can pick (native-triggers.md): the tutorial included
HIDDEN = 7  # Terran05b, Zerg04b-f and Bonus
EVIDENCE = ROOT / "docs/research/evidence/native-triggers-static.json"  # private (.gitignore)


class CampaignTest(unittest.TestCase):
    def test_counts(self):
        m = campaign.load(PROFILE)
        playable = [r for t in m.tables for r in t.rows if r[1] != 0 and r[2] == 0]
        assert sum(1 for r in playable if not r[4]) == SELECTABLE
        assert sum(1 for r in playable if r[4]) == HIDDEN

    @unittest.skipIf(not EVIDENCE.exists(), "no research evidence")
    def test_manifest_matches_evidence(self):
        m = campaign.load(PROFILE)
        ev = json.loads(EVIDENCE.read_text())["campaigns"]
        by_name = {c["name"]: c for c in ev}
        for t in m.tables:
            rows = by_name[t.name]["rows"]
            assert t.rows == tuple((r["label_id_unresolved"], r["map_index"], r["movie_id"], r["race"], r["hidden"])
                                   for r in rows), t.name

    def test_container_layout(self):
        m = campaign.load(PROFILE)
        dirs = [b""] + [b"campaign\\x\\m%02d" % k for k in range(1, campaign.MISSIONS)]
        c = parse(campaign.build(m, dirs, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes())
        tabl = c.section("TABL").payload
        assert tabl[0] == 6
        exp, race, count = struct.unpack_from("<BBH", tabl, 1)
        assert (exp, race, count) == (0, 0, len(m.tables[0].rows))
        assert campaign.ROW.unpack_from(tabl, 5) == m.tables[0].rows[0]
        mapd = c.section("MAPD").payload
        assert struct.unpack_from("<HH", mapd, 0) == (campaign.MISSIONS, 0)
        carried, n = struct.unpack_from("<IH", c.section("CHEA").payload, 0)
        assert (carried, n) == (0x28000200, len(m.cheats))

    @needs_original
    def test_tables_match_executable(self):
        from converter.pe import PeImage

        m = campaign.load(PROFILE)
        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        pointers = [*struct.unpack("<3I", pe.read_va(m.tables_va["original"], 12)),
                    *struct.unpack("<3I", pe.read_va(m.tables_va["expansion"], 12))]
        assert pointers == [t.va for t in m.tables]
        for t in m.tables:
            raw = pe.read_va(t.va, campaign.ROW.size * len(t.rows))
            assert raw == b"".join(campaign.ROW.pack(*r) for r in t.rows), t.name
        for va, lo, hi, _, text in m.cheats:
            assert pe.read_va(va, 8) == struct.pack("<II", lo, hi), text

    @needs_original
    def test_directories(self):
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL, full_hash=False) as r:
            dirs = campaign.directories(r.read("arr\\mapdata.dat")[1], r.read("arr\\mapdata.tbl")[1])
        assert len(dirs) == campaign.MISSIONS
        assert dirs[12] == b"campaign\\zerg\\zerg01"
        assert dirs[1] == b"campaign\\terran\\tutorial"


if __name__ == "__main__":
    unittest.main()
