"""PNG images on the standard library (converter/raster.py). Where Pillow happens to be installed,
the files are also checked against it in both directions; the converter itself does not use it."""
from __future__ import annotations

import io
import random
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from converter import raster

try:
    from PIL import Image as PilImage
except ImportError:  # the converter does not need Pillow
    PilImage = None

has_pillow = unittest.skipIf(PilImage is None, "Pillow not installed")


def noise(n: int, seed: int) -> bytes:
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(n))


def png_with_filters(image: raster.Image, kinds: list[int]) -> bytes:
    """A PNG of `image` whose row y uses filter kinds[y % len(kinds)] (encoder for the tests)."""
    n = image.channels
    stride = image.width * n
    raw, prev = bytearray(), bytes(stride)
    for y in range(image.height):
        line = image.data[y * stride : (y + 1) * stride]
        kind = kinds[y % len(kinds)]
        out = bytearray(stride)
        for i in range(stride):
            left = line[i - n] if i >= n else 0
            up = prev[i]
            upper_left = prev[i - n] if i >= n else 0
            predict = {0: 0, 1: left, 2: up, 3: (left + up) >> 1, 4: raster._paeth(left, up, upper_left)}[kind]
            out[i] = (line[i] - predict) & 0xFF
        raw += bytes([kind]) + out
        prev = bytes(line)
    colour = raster.MODES[image.mode][1]
    chunks = [raster._chunk(b"IHDR", struct.pack(">IIBBBBB", image.width, image.height, 8, colour, 0, 0, 0))]
    if image.mode == "P":
        chunks.append(raster._chunk(b"PLTE", image.palette))
    chunks += [raster._chunk(b"IDAT", zlib.compress(bytes(raw))), raster._chunk(b"IEND", b"")]
    return raster._SIGNATURE + b"".join(chunks)


class RasterTest(unittest.TestCase):
    def test_round_trip_every_mode(self) -> None:
        for mode, (n, _) in raster.MODES.items():
            with self.subTest(mode=mode):
                image = raster.frombytes(mode, (7, 5), noise(7 * 5 * n, n))
                if mode == "P":
                    image.putpalette(noise(768, 9))
                back = raster.decode_png(raster.encode_png(image))
                self.assertEqual((back.mode, back.size, back.tobytes()), (mode, (7, 5), image.tobytes()))
                if mode == "P":
                    self.assertEqual(back.palette, image.palette)

    def test_every_row_filter_decodes(self) -> None:
        for mode, (n, _) in raster.MODES.items():
            with self.subTest(mode=mode):
                image = raster.frombytes(mode, (9, 10), noise(9 * 10 * n, 3 + n))
                if mode == "P":
                    image.putpalette(noise(768, 4))
                back = raster.decode_png(png_with_filters(image, [0, 1, 2, 3, 4]))
                self.assertEqual(back.tobytes(), image.tobytes())

    def test_new_fill_paste_and_pixels(self) -> None:
        canvas = raster.new("LA", (4, 3), (0, 0))
        canvas.paste(raster.frombytes("LA", (2, 2), bytes([1, 255, 2, 255, 3, 255, 4, 255])), (1, 1))
        self.assertEqual(canvas.getpixel((1, 1)), (1, 255))
        self.assertEqual(canvas.getpixel((2, 2)), (4, 255))
        self.assertEqual(canvas.getpixel((0, 0)), (0, 0))
        clipped = raster.new("L", (3, 3), 9)
        clipped.paste(raster.frombytes("L", (2, 2), bytes([1, 2, 3, 4])), (2, -1))
        self.assertEqual(clipped.tobytes(), bytes([9, 9, 3, 9, 9, 9, 9, 9, 9]))
        self.assertEqual(raster.new("RGBA", (1, 1), 7).getpixel((0, 0)), (7, 7, 7, 7))

    def test_png_size_reads_the_header(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "a.png"
            path.write_bytes(raster.encode_png(raster.new("L", (300, 2))))
            self.assertEqual(raster.png_size(path), (300, 2))
            self.assertEqual(raster.open_png(path).size, (300, 2))

    def test_rejects_what_it_does_not_handle(self) -> None:
        with self.assertRaises(raster.RasterError):
            raster.frombytes("L", (2, 2), b"abc")
        with self.assertRaises(raster.RasterError):
            raster.new("I;16", (1, 1))
        with self.assertRaises(raster.RasterError):
            raster.new("L", (2, 2)).paste(raster.new("LA", (1, 1)), (0, 0))
        with self.assertRaises(raster.RasterError):
            raster.decode_png(b"GIF89a")
        sixteen = raster.encode_png(raster.new("L", (1, 1))).replace(
            struct.pack(">IIBB", 1, 1, 8, 0), struct.pack(">IIBB", 1, 1, 16, 0))
        with self.assertRaises(raster.RasterError):
            raster.decode_png(sixteen)

    @has_pillow
    def test_pillow_reads_ours(self) -> None:
        for mode, (n, _) in raster.MODES.items():
            with self.subTest(mode=mode):
                image = raster.frombytes(mode, (13, 6), noise(13 * 6 * n, 20 + n))
                if mode == "P":
                    image.putpalette(noise(768, 21))
                pil = PilImage.open(io.BytesIO(raster.encode_png(image)))
                self.assertEqual((pil.mode, pil.size, pil.tobytes()), (mode, (13, 6), image.tobytes()))
                if mode == "P":
                    self.assertEqual(bytes(pil.getpalette()), image.palette)

    @has_pillow
    def test_we_read_pillows(self) -> None:
        for mode, (n, _) in raster.MODES.items():
            with self.subTest(mode=mode):
                data = noise(31 * 17 * n, 40 + n)
                pil = PilImage.frombytes(mode, (31, 17), data)
                if mode == "P":
                    pil.putpalette(list(noise(768, 41)))
                buf = io.BytesIO()
                pil.save(buf, format="PNG", optimize=True)  # adaptive row filters
                back = raster.decode_png(buf.getvalue())
                self.assertEqual((back.mode, back.size, back.tobytes()), (mode, (31, 17), data))


if __name__ == "__main__":
    unittest.main()
