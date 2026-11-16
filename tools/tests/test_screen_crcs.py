"""A custom golden's screen CRCs (tools/state_check.py's screen_crcs over sc_emulator/uiscreen.py's capture format).

The Play Custom screen's capture at Ok (custom.py's play_custom.cap) stays private; its golden keeps CRC-32s of the
whole screen and of each band of rows, with the cursor's rectangle zeroed, because a capture between two presents
still has the cursor. test_flow.play_custom_screens_are_the_originals zeroes the same rectangle in Koprulu's screen,
so the rectangle and the bands must be what this test pins. Synthetic captures only: no emulator run, no game data;
the capture module imports the emulator's package, so the test needs unicorn (the research venv) and skips without.
"""
from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "research"))

HAS_UNICORN = importlib.util.find_spec("unicorn") is not None
if HAS_UNICORN:
    import state_check  # noqa: E402
    from sc_emulator import uiscreen  # noqa: E402

WIDTH, HEIGHT = 640, 480


def write_capture(path: Path, pixels: bytes, cursor: dict) -> None:
    """A capture as uiscreen.capture_screen writes one."""
    header = {"size": [WIDTH, HEIGHT], "cursor": cursor, "dialogs": [], "glue_state": 11}
    uiscreen._write(path, header, [("pixels", zlib.compress(pixels, 6)), ("palette", bytes(1024))])


@unittest.skipUnless(HAS_UNICORN, "the capture module imports the emulator's package, which needs unicorn")
class ScreenCrcsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.pixels = bytes((x * 7 + y * 13) & 0xFF for y in range(HEIGHT) for x in range(WIDTH))

    def crcs(self, pixels: bytes, cursor: dict) -> dict:
        path = Path(self.dir.name) / "play_custom.cap"
        write_capture(path, pixels, cursor)
        return state_check.screen_crcs(path)

    def test_without_a_cursor_the_crcs_are_the_pixels(self) -> None:
        out = self.crcs(self.pixels, {"enabled": 0, "rect": [10, 10, 20, 21]})
        band = WIDTH * state_check.CAPTURE_BAND
        self.assertNotIn("cursor", out)
        self.assertEqual(out["band_rows"], state_check.CAPTURE_BAND)
        self.assertEqual(out["crc"], zlib.crc32(self.pixels))
        self.assertEqual(len(out["bands"]), HEIGHT // state_check.CAPTURE_BAND)
        self.assertEqual(out["bands"], [zlib.crc32(self.pixels[k:k + band]) for k in range(0, len(self.pixels), band)])

    def test_the_cursor_is_zeroed_and_clipped(self) -> None:
        cursor = {"enabled": 1, "rect": [630, 470, 20, 21]}  # past the right and bottom edges
        out = self.crcs(self.pixels, cursor)
        self.assertEqual(out["cursor"], [630, 470, 10, 10])
        expected = bytearray(self.pixels)
        for y in range(470, HEIGHT):
            expected[y * WIDTH + 630:(y + 1) * WIDTH] = bytes(10)
        self.assertEqual(out["crc"], zlib.crc32(expected))
        # What the cursor covers does not change the CRCs; a pixel outside it does.
        other = bytearray(self.pixels)
        other[475 * WIDTH + 635] ^= 0xFF
        self.assertEqual(self.crcs(bytes(other), cursor)["crc"], out["crc"])
        other[0] ^= 0xFF
        moved = self.crcs(bytes(other), cursor)
        self.assertNotEqual(moved["crc"], out["crc"])
        self.assertNotEqual(moved["bands"][0], out["bands"][0])
        self.assertEqual(moved["bands"][1:], out["bands"][1:])

    def test_a_cursor_off_the_screen_covers_nothing(self) -> None:
        out = self.crcs(self.pixels, {"enabled": 1, "rect": [-30, -40, 20, 21]})
        self.assertEqual(out["cursor"], [0, 0, 0, 0])
        self.assertEqual(out["crc"], zlib.crc32(self.pixels))


if __name__ == "__main__":
    unittest.main()
