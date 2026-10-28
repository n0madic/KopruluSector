"""The icon files (converter/icon.py): synthetic DIBs and images, and StarCraft.exe's icon when KOPRULU_ORIGINAL is set."""
from __future__ import annotations

import os
import struct
import unittest
from pathlib import Path

from converter import icon, raster
from converter.mpq.profile import Profile
from converter.pe import PeImage

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


def dib(width: int, rgba: bytes) -> bytes:
    """A 32-bit icon DIB (bottom-up BGRA rows, then the mask) of the top-down RGBA pixels."""
    stride = width * 4
    rows = [rgba[y * stride : (y + 1) * stride] for y in range(width)]
    bgra = b"".join(
        bytes(v for px in (row[i : i + 4] for i in range(0, stride, 4)) for v in (px[2], px[1], px[0], px[3]))
        for row in reversed(rows))
    header = struct.pack("<IiiHHIIiiII", 40, width, 2 * width, 1, 32, 0, len(bgra), 0, 0, 0, 0)
    return header + bgra + bytes(((width + 31) // 32 * 4) * width)


def gradient(width: int) -> raster.Image:
    data = bytearray()
    for y in range(width):
        for x in range(width):
            data += bytes([x * 255 // (width - 1), y * 255 // (width - 1), 90, 255 - x * 255 // (width - 1) // 2])
    return raster.frombytes("RGBA", (width, width), bytes(data))


class IconTest(unittest.TestCase):
    def test_dib_to_image_flips_rows_and_reorders_channels(self) -> None:
        image = gradient(4)
        back = icon.dib_to_image(dib(4, image.tobytes()))
        self.assertEqual(back.size, (4, 4))
        self.assertEqual(back.tobytes(), image.tobytes())

    def test_dib_rejects_what_is_not_a_32_bit_square(self) -> None:
        good = bytearray(dib(4, gradient(4).tobytes()))
        for field, value in (("bits", 8), ("height", 4)):
            bad = bytearray(good)
            if field == "bits":
                struct.pack_into("<H", bad, 14, value)
            else:
                struct.pack_into("<i", bad, 8, value)
            with self.subTest(field=field), self.assertRaises(icon.IconError):
                icon.dib_to_image(bytes(bad))
        with self.assertRaises(icon.IconError):
            icon.dib_to_image(bytes(good[:60]))

    def test_resize_keeps_a_flat_colour_and_its_alpha(self) -> None:
        flat = raster.frombytes("RGBA", (8, 8), bytes([200, 100, 50, 128]) * 64)
        for size in (5, 8, 20):
            with self.subTest(size=size):
                out = icon.resize(flat, size)
                self.assertEqual(out.size, (size, size))
                self.assertEqual(set(zip(*[iter(out.tobytes())] * 4)), {(200, 100, 50, 128)})

    def test_resize_does_not_bleed_the_colour_of_transparent_pixels(self) -> None:
        # opaque red on the left, fully transparent (and green) on the right: the edge stays red, never brownish
        data = bytearray()
        for _y in range(4):
            data += bytes([255, 0, 0, 255]) * 2 + bytes([0, 255, 0, 0]) * 2
        out = icon.resize(raster.frombytes("RGBA", (4, 4), bytes(data)), 16)
        for i in range(16 * 16):
            r, g, b, a = out.data[4 * i : 4 * i + 4]
            if a:
                self.assertEqual((r, g, b), (255, 0, 0))

    def test_resize_up_keeps_the_corners(self) -> None:
        image = gradient(4)
        out = icon.resize(image, 16)
        self.assertEqual(out.getpixel((0, 0)), image.getpixel((0, 0)))
        self.assertEqual(out.getpixel((15, 15)), image.getpixel((3, 3)))

    def test_flatten_and_centred(self) -> None:
        clear = raster.new("RGBA", (2, 2), (255, 255, 255, 0))
        self.assertEqual(icon.flatten(clear, (1, 2, 3)).getpixel((1, 1)), (1, 2, 3))
        solid = raster.new("RGBA", (2, 2), (9, 8, 7, 255))
        self.assertEqual(icon.flatten(solid, (1, 2, 3)).getpixel((0, 0)), (9, 8, 7))
        out = icon.centred(solid, 6)
        self.assertEqual(out.getpixel((0, 0))[3], 0)
        self.assertEqual(out.getpixel((2, 2)), (9, 8, 7, 255))

    def test_ico_and_icns_lead_to_their_images(self) -> None:
        images = [gradient(16), gradient(32)]
        ico = icon.encode_ico(images)
        self.assertEqual(struct.unpack_from("<HHH", ico, 0), (0, 1, 2))
        for i, image in enumerate(images):
            w, h, _c, _r, planes, bits, size, offset = struct.unpack_from("<BBBBHHII", ico, 6 + 16 * i)
            self.assertEqual((w, h, planes, bits), (image.width, image.height, 1, 32))
            self.assertEqual(raster.decode_png(ico[offset : offset + size]).tobytes(), image.tobytes())
        png = raster.encode_png(images[0])
        icns = icon.encode_icns([(b"icp4", png), (b"ic11", png)])
        self.assertEqual(icns[:4], b"icns")
        self.assertEqual(struct.unpack_from(">I", icns, 4)[0], len(icns))
        self.assertEqual(icns[8:12], b"icp4")
        self.assertEqual(struct.unpack_from(">I", icns, 12)[0], 8 + len(png))

    def test_write_set_makes_each_platforms_files(self) -> None:
        import tempfile

        icons = {16: gradient(16), 32: gradient(32), 48: gradient(48), 64: gradient(64)}
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            names = {p.relative_to(out).as_posix() for p in icon.write_set(icons, out)}
            for name in ("icon.ico", "icon.icns", "icon-256.png", "web/favicon.ico", "web/apple-touch-icon.png",
                         "ios/AppIcon60x60@3x.png", "android/res/mipmap-xxxhdpi/ic_launcher_foreground.png",
                         "android/res/mipmap-anydpi-v26/ic_launcher.xml", "android/res/values/ic_launcher_background.xml"):
                self.assertIn(name, names)
            self.assertEqual(raster.open_png(out / "icon-64.png").tobytes(), icons[64].tobytes())  # an original size is as it is
            self.assertEqual(raster.png_size(out / "icon-256.png"), (256, 256))
            self.assertEqual(raster.png_size(out / "android/res/mipmap-hdpi/ic_launcher.png"), (72, 72))
            self.assertEqual(raster.png_size(out / "android/res/mipmap-xxhdpi/ic_launcher_foreground.png"), (324, 324))
            self.assertEqual(raster.open_png(out / "ios/AppIcon60x60@2x.png").mode, "RGB")  # iOS takes no alpha

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_the_executables_icons(self) -> None:
        profile = Profile.load()
        icons = icon.read_icons(PeImage.load(Path(ORIGINAL) / profile.executable.name))
        self.assertEqual(sorted(icons), [16, 32, 48, 64])
        self.assertTrue(all(i.mode == "RGBA" for i in icons.values()))
        big = icons[64]
        self.assertTrue(any(big.data[3::4]) and not all(a == 255 for a in big.data[3::4]))  # art over a transparent ground


if __name__ == "__main__":
    unittest.main()
