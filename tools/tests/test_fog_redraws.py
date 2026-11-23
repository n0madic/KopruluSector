"""tools/fog_check.py's check of the original's partial redraws (ADR 0009) and fogcap.py's redraws.cap records.

A partial redraw shades only the 8 x 8 blocks of its dirty cells and leaves the others as an earlier redraw shaded
them; `ShadedView` follows each block's corner levels through the redraws and names the blocks left with levels that
changed since. fog_check imports the emulator's package, which needs unicorn (the research venv), so the tests skip
without it.
"""
from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HAS_UNICORN = importlib.util.find_spec("unicorn") is not None

LEVEL_STRIDE, LEVEL_ROWS = 88, 60
DIRTY_COLS, VIEW_CELL_ROWS = 40, 25
SEEN = 31


def shaded_view():
    if str(ROOT / "tools") not in sys.path:
        sys.path.insert(0, str(ROOT / "tools"))
    import fog_check
    return fog_check.ShadedView()


def levels_with(*cells: tuple[int, int, int]) -> bytes:
    """A level map all seen but the (row, column, level) cells."""
    levels = bytearray([SEEN]) * (LEVEL_STRIDE * LEVEL_ROWS)
    for row, col, level in cells:
        levels[row * LEVEL_STRIDE + col] = level
    return bytes(levels)


def dirty_cells(*cells: tuple[int, int]) -> bytes:
    """The view's dirty grid with the (column, row) cells marked."""
    grid = bytearray(DIRTY_COLS * VIEW_CELL_ROWS)
    for cx, cy in cells:
        grid[cy * DIRTY_COLS + cx] = 1
    return bytes(grid)


@unittest.skipUnless(HAS_UNICORN, "fog_check imports sc_emulator, which needs unicorn (the research venv)")
class ShadedViewTest(unittest.TestCase):
    def test_a_partial_redraw_of_every_changed_block_leaves_none_stale(self):
        view = shaded_view()
        view.redraw(True, (0, 0), levels_with(), dirty_cells())
        # level map cell (10, 20) is a corner of blocks 19..20 x 9..10: cells (9..10, 4..5) at camera (0, 0)
        changed = levels_with((10, 20, 15))
        self.assertEqual(view.redraw(False, (0, 0), changed, dirty_cells((9, 4), (10, 4), (9, 5), (10, 5))), [])
        self.assertEqual(view.redraw(False, (0, 0), changed, dirty_cells()), [])

    def test_a_block_left_with_changed_levels_is_stale_until_redrawn(self):
        view = shaded_view()
        view.redraw(True, (0, 0), levels_with(), dirty_cells())
        changed = levels_with((10, 20, 15))
        stale = view.redraw(False, (0, 0), changed, dirty_cells((10, 5)))
        self.assertEqual(sorted((x, y) for x, y, _, _ in stale), [(152, 72), (152, 80), (160, 72)])
        self.assertIn((160, 72, (SEEN, SEEN, SEEN, SEEN), (SEEN, SEEN, 15, SEEN)), stale)
        self.assertEqual(len(view.redraw(False, (0, 0), changed, dirty_cells((9, 4)))), 2)
        self.assertEqual(view.redraw(False, (0, 0), changed, dirty_cells((10, 4), (9, 5))), [])

    def test_the_camera_picks_the_block_in_the_tile(self):
        view = shaded_view()
        view.redraw(True, (8, 16), levels_with(), dirty_cells())
        # at (8, 16) the view's block (i, j) reads the level map from (2 + j, 1 + i): cell (10, 20) is a corner of
        # blocks 18..19 x 7..8, the cells (9, 3..4)
        stale = view.redraw(False, (8, 16), levels_with((10, 20, 0)), dirty_cells())
        self.assertEqual(sorted((x, y) for x, y, _, _ in stale), [(144, 56), (144, 64), (152, 56), (152, 64)])

    def test_a_full_redraw_shades_every_block(self):
        view = shaded_view()
        view.redraw(True, (0, 0), levels_with(), dirty_cells())
        changed = levels_with((10, 20, 15), (40, 70, 0))
        self.assertEqual(view.redraw(True, (0, 0), changed, dirty_cells()), [])
        self.assertEqual(view.redraw(False, (0, 0), changed, dirty_cells()), [])


@unittest.skipUnless(HAS_UNICORN, "fogcap imports the emulator, which needs unicorn (the research venv)")
class RedrawRecordsTest(unittest.TestCase):
    def test_records_read_back_with_their_tile_flags_whole(self):
        sys.path.insert(0, str(ROOT / "tools" / "research"))
        from sc_emulator import fogcap
        tiles = [bytes(range(256)) * 64, bytes(255 - (i % 256) for i in range(256 * 64)), bytes(256 * 64),
                 bytes([7]) * (256 * 96)]  # the last one a map of another size
        records = []
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / fogcap.REDRAWS
            previous = b""
            with open(path, "wb") as f:
                for k, t in enumerate(tiles):
                    header = {"frame": k, "full": k == 0, "camera": [8 * k, 0], "outside": 0}
                    dirty, levels = dirty_cells((k, k)), levels_with((k, k, k))
                    f.write(fogcap.redraw_record(header, dirty, levels, t, previous))
                    records.append((dirty, levels, t))
                    previous = t
            read = list(fogcap.redraws(path))
        self.assertEqual([h["frame"] for h, _ in read], [0, 1, 2, 3])
        self.assertEqual([h["tiles_xor"] for h, _ in read], [False, True, True, False])
        for (h, blobs), (dirty, levels, t) in zip(read, records):
            self.assertEqual((blobs["dirty"], blobs["levels"], blobs["tiles"]), (dirty, levels, t), h["frame"])


if __name__ == "__main__":
    unittest.main()
