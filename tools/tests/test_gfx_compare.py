"""tools/gfx_compare.py, the SDL_GPU backend's frame check against GL (ADR 0028, stage S9.5)."""
from __future__ import annotations

import io
import logging
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stderr
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import gfx_compare  # noqa: E402

WIDTH, HEIGHT = 5, 5  # a row a PNG filter (0..4)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def _filtered(row: bytes, prior: bytes, kind: int, channels: int) -> bytes:
    """The row filtered by its PNG filter type, as an encoder writes it."""
    out = bytearray(len(row))
    for i, x in enumerate(row):
        left = row[i - channels] if i >= channels else 0
        upper_left = prior[i - channels] if i >= channels else 0
        predictor = (0, left, prior[i], (left + prior[i]) >> 1, _paeth(left, prior[i], upper_left))[kind]
        out[i] = (x - predictor) & 0xFF
    return bytes(out)


def _png(path: Path, pixels: bytes, channels: int) -> None:
    """A WIDTH x HEIGHT PNG whose row y takes filter type y."""
    stride, prior, raw = WIDTH * channels, bytes(WIDTH * channels), b""
    for y in range(HEIGHT):
        row = pixels[y * stride:(y + 1) * stride]
        raw += bytes([y]) + _filtered(row, prior, y, channels)
        prior = row

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6 if channels == 4 else 2, 0, 0, 0)
    path.write_bytes(gfx_compare.PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw))
                     + chunk(b"IEND", b""))


def _pixels(channels: int, seed: int) -> bytes:
    return bytes((seed * 37 + k * 91 + (k * k) % 53) & 0xFF for k in range(WIDTH * HEIGHT * channels))


class GfxCompareTest(unittest.TestCase):
    def setUp(self):
        logging.disable(logging.CRITICAL)
        self.addCleanup(logging.disable, logging.NOTSET)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.gl, self.gpu = Path(self.tmp.name) / "gl", Path(self.tmp.name) / "gpu"
        self.gl.mkdir()
        self.gpu.mkdir()

    def _run(self, *args: str) -> int:
        with redirect_stderr(io.StringIO()):
            return gfx_compare.main([str(self.gl), str(self.gpu), *args])

    def test_every_filter_decodes(self):
        for channels in (3, 4):
            pixels = _pixels(channels, channels)
            _png(self.gl / "a.png", pixels, channels)
            image = gfx_compare.read_png(self.gl / "a.png")
            self.assertEqual((image.width, image.height, image.channels), (WIDTH, HEIGHT, channels))
            self.assertEqual(image.pixels, pixels)

    def test_the_same_frames_pass_exactly(self):
        pixels = _pixels(4, 1)
        _png(self.gl / "a.png", pixels, 4)
        _png(self.gpu / "a.png", pixels, 4)
        self.assertEqual(self._run(), 0)

    def test_a_difference_within_the_limit(self):
        pixels = bytearray(_pixels(3, 2))
        _png(self.gl / "a.png", bytes(pixels), 3)
        pixels[7] = (pixels[7] + 1) & 0xFF
        _png(self.gpu / "a.png", bytes(pixels), 3)
        d = gfx_compare.compare(gfx_compare.read_png(self.gl / "a.png"), gfx_compare.read_png(self.gpu / "a.png"))
        self.assertEqual((d.differ, d.largest, d.at), (1, 1, (2, 0)))
        self.assertEqual(self._run(), 1)  # exact by default
        self.assertEqual(self._run("--max-diff", "1"), 0)
        self.assertEqual(self._run("--max-diff", "0"), 1)

    def test_alpha_is_not_compared(self):
        pixels = bytearray(_pixels(4, 3))
        _png(self.gl / "a.png", bytes(pixels), 4)
        pixels[3] ^= 0xFF
        _png(self.gpu / "a.png", bytes(pixels), 4)
        self.assertEqual(self._run(), 0)

    def test_a_frame_in_one_run_only_fails(self):
        _png(self.gl / "a.png", _pixels(3, 4), 3)
        self.assertEqual(self._run(), 1)
        self.assertEqual(self._run("--max-diff", "255"), 1)

    def test_no_frames_fail(self):
        self.assertEqual(self._run(), 1)


if __name__ == "__main__":
    unittest.main()
