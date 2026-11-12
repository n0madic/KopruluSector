"""The in-memory conversion (ADR 0018, stage D8d): the packs record `scr-manifest` writes and the game data's containers
(scdata/gamedata.py, scdata/missions.py); with KOPRULU_ORIGINAL the 1.17 install's."""
from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path

from scdata import gamedata, missions
from scdata.formats.palette import TILESETS

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
PROFILE = "bw-1.17.0.1"
PACKS = {"campaign\\terran\\terran01": ["doodads", "effects"], "Campaign\\Zerg\\zerg01": ["zerg"]}
SCENARIO_PACKS = {"zerg_build": ["zerg"], "ai_queen": ["effects", "zerg"]}


class PacksRecordTest(unittest.TestCase):
    def test_the_packs_are_written_by_directory_and_read_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "packs.json"
            missions.write_packs(path, PROFILE, PACKS, SCENARIO_PACKS)
            text = path.read_text()
            assert text.index("terran01") < text.index("zerg01")  # sorted without regard to case
            assert text.index("ai_queen") < text.index("zerg_build")
            assert text.count("\n") == 12 and text.endswith("]\n }\n}\n")  # one mission or scenario a line
            data = json.loads(text)
            assert data["missions"] == PACKS and data["scenarios"] == SCENARIO_PACKS

    def test_the_repositorys_record_names_the_catalog_missions(self):
        packs = missions.load_packs(PROFILE)
        assert len(packs) == 64 and all(p == sorted(p) for p in packs.values())
        assert packs["campaign\\terran\\terran01"] == ["doodads", "effects", "neutral", "terran", "zerg"]


@unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
class OriginalTest(unittest.TestCase):
    def test_the_game_data_is_built_in_memory(self):
        from scdata.mpq.profile import Resolver

        with Resolver(ORIGINAL) as r:
            built = gamedata.build(r)
            assert sorted(built) == sorted(["dat", "strings", "iscript", "aiscript", "fonts", "ui", "sounds", "palettes",
                                            "portraits", "grpmeta", "campaign", *(f"tilesets/{n}" for n in TILESETS)])
            assert all(c.source_hash == r.data_set_hash() and c.profile_id == PROFILE for c in built.values())
            assert gamedata.strings(r).to_bytes() == built["strings"].to_bytes()
