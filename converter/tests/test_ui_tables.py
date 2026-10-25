"""The interface's tables (defs/ui/tables-*.json) against the executable and Local.dll."""
from __future__ import annotations

import os
import unittest
from pathlib import Path

from converter import ui_tables

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
PROFILE = "bw-1.17.0.1"
BUTTONS = 896  # buttons over the 250 sets (89 sets are empty)
EMPTY_SETS = 89


class UiTablesTest(unittest.TestCase):
    def test_counts(self):
        t = ui_tables.load(PROFILE)
        assert len(t["button_sets"]) == ui_tables.BUTTON_SET_COUNT
        assert sum(len(s["buttons"]) for s in t["button_sets"]) == BUTTONS
        assert sum(1 for s in t["button_sets"] if not s["buttons"]) == EMPTY_SETS
        assert len(t["status_layouts"]) == ui_tables.STATUS_LAYOUT_COUNT
        assert [len(f["rects"]) for f in t["fluff"]] == [6, 5, 4, 7]
        assert len(t["cursors"]) == ui_tables.CURSOR_COUNT and t["cursors"][0] == "arrow.grp"
        assert t["rects"] == {"command_card": [496, 354, 639, 479], "minimap": [6, 348, 133, 475]}
        assert len(t["establishing"]) == ui_tables.ESTABLISHING_COUNT and t["finale"] == [["FinZ09bx", 63]]

    def test_every_function_is_named(self):
        t = ui_tables.load(PROFILE)
        for table in ("conditions", "actions", "status_refresh", "status_draw"):
            assert all(name for _, name in t[table]), table
        used = {b[2] for s in t["button_sets"] for b in s["buttons"]}
        assert used == {va for va, _ in t["conditions"]}
        assert [s["link"] for s in t["button_sets"]].count(0xFFFF) == ui_tables.BUTTON_SET_COUNT - 2  # 16->1, 19->2

    def test_kpd_sections(self):
        from converter.kpf import kpd

        t = ui_tables.load(PROFILE)
        sections = dict(kpd.ui_table_sections(t))
        assert len(sections["BTNS"]) == 2 + 4 * ui_tables.BUTTON_SET_COUNT + 20 * BUTTONS
        assert len(sections["STLY"]) == 2 + 8 * ui_tables.STATUS_LAYOUT_COUNT
        assert len(sections["VOLT"]) == 1 + 4 * ui_tables.ATTENUATION_COUNT
        assert sections["BTNS"][2:4] == b"\xff\xff"  # set 0 links no set
        assert sections["ESTB"][0] == ui_tables.ESTABLISHING_COUNT + 1

    @needs_original
    def test_tables_match_executable(self):
        from converter.pe import PeImage

        local = Path(ORIGINAL) / "Local.dll"
        got = ui_tables.extract(PeImage.load(Path(ORIGINAL) / "StarCraft.exe"),
                                PeImage.load(local) if local.exists() else None)
        want = ui_tables.load(PROFILE)
        if not local.exists():  # the retail extract has no Local.dll: its accelerators are checked where it is
            want["accelerators"] = [a for a in want["accelerators"] if a["module"] != "Local.dll"]
        assert {**got, "profile": PROFILE} == want


if __name__ == "__main__":
    unittest.main()
