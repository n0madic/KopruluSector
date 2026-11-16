"""The emulator's file system (tools/research/sc_emulator/vfs.py) lists directories as Windows does.

The original's screens show what FindFirstFileA gives: the map list makes "[Up One Level]" from the ".." entry
(AddFolderEntry 0x45b080) and shows a folder by the name the file system keeps, so a folder the game creates
(InitMapList 0x455aa0's maps\\replays) must keep its case. Both reach the Play Custom screen's capture, which
test_flow.play_custom_screens_are_the_originals compares with Koprulu's. Stdlib only: no unicorn, no game data.
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "research"))

from sc_emulator import vfs  # noqa: E402


class EmulatorVfsTest(unittest.TestCase):
    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        game = Path(tmp.name)
        (game / "maps" / "BroodWar").mkdir(parents=True)
        (game / "maps" / "Challenger.scm").write_bytes(b"x")
        (game / "maps" / "BroodWar" / "Astral.scx").write_bytes(b"xy")
        self.fs = vfs.FileSystem(game)

    def names(self, pattern: str) -> list[str]:
        return [name for name, _, _ in self.fs.listdir(pattern)]

    def test_a_subdirectory_lists_its_dots_first(self) -> None:
        self.assertEqual(self.names("C:\\StarCraft\\maps\\BroodWar\\*.*"), [".", "..", "Astral.scx"])
        self.assertEqual(self.names("C:\\StarCraft\\maps\\*.*"), [".", "..", "BroodWar", "Challenger.scm"])
        self.assertTrue(all(is_dir for name, is_dir, _ in self.fs.listdir("C:\\StarCraft\\maps\\*") if name in (".", "..")))

    def test_a_pattern_with_an_extension_lists_no_dots(self) -> None:
        self.assertEqual(self.names("C:\\StarCraft\\maps\\*.scm"), ["Challenger.scm"])

    def test_a_drive_root_and_a_missing_directory_list_no_dots(self) -> None:
        self.assertNotIn("..", self.names("C:\\*.*"))
        self.assertEqual(self.names("C:\\StarCraft\\nothing\\*.*"), [])

    def test_a_created_directory_keeps_its_case(self) -> None:
        self.assertEqual(self.fs.mkdir("C:\\StarCraft\\maps\\replays"), 0)
        self.assertIn("replays", self.names("C:\\StarCraft\\maps\\*.*"))
        self.assertEqual(self.names("C:\\StarCraft\\maps\\REPLAYS\\*.*"), [".", ".."])


if __name__ == "__main__":
    unittest.main()
