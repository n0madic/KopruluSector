"""The game's icon for the product's bundles: the 32-bit images of StarCraft.exe's icon group, made into each
platform's icon files (`python3 -m converter icon`; docs/phaseS.md, "Icons").

The executable carries the icon at 16, 32, 48 and 64 pixels (an RT_GROUP_ICON of RGBA DIBs); the platforms want
more sizes than that (a favicon, a launcher's, a dock's), which are made from the 64-pixel image by a bilinear
resampling on premultiplied colour. The art is the original's: the files go to the converter's output
(generated/icon/), never to the repository.
"""
from __future__ import annotations

import logging
import struct
from pathlib import Path

from . import raster
from .errors import ConverterError
from .mpq.profile import Profile, verify_file
from .pe import PeImage

log = logging.getLogger("converter")

RT_ICON = 3
RT_GROUP_ICON = 14
ICON_GROUP = 102  # the executable's main icon group (the second, 106 pixels of it, is the cursor-sized one)

# The opaque ground of the icons that cannot be transparent (iOS's, the Android adaptive icon's), a dark steel
# like the shadow's colour in the art.
BACKGROUND = (16, 20, 28)
# The Android adaptive icon's foreground fills this part of its canvas (the launcher masks everything outside the
# middle 66 of 108 dp).
ADAPTIVE_FILL = 0.66
# iOS rounds the corners of its square: the art keeps this much of it so that the wing tips stay inside
IOS_FILL = 0.88

# the sizes of each platform's files
ICO_SIZES = (16, 32, 48, 64, 128, 256)
FAVICON_ICO_SIZES = (16, 32, 48)
# macOS's icns: OSType -> size (all PNG; ic11/ic12/ic13/ic14 are the 16, 32, 128 and 256 pixel images at 2x)
ICNS_TYPES = {b"icp4": 16, b"icp5": 32, b"icp6": 64, b"ic07": 128, b"ic08": 256, b"ic09": 512, b"ic11": 32,
              b"ic12": 64, b"ic13": 256, b"ic14": 512}
# iOS's loose icons (CFBundleIconFiles): file name -> pixels
IOS_ICONS = {"AppIcon60x60@2x": 120, "AppIcon60x60@3x": 180, "AppIcon76x76@2x": 152, "AppIcon83.5x83.5@2x": 167}
# Android's launcher icons by density: legacy square icon (48 dp), adaptive foreground canvas (108 dp)
ANDROID_DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
ANDROID_LEGACY_DP = 48
ANDROID_ADAPTIVE_DP = 108
WEB_ICONS = {"favicon-16x16": 16, "favicon-32x32": 32, "apple-touch-icon": 180}
WINDOW_ICON_SIZE = 256  # the one the Linux build loads for its window (native/kp_app/sdl/kp_app_main.c)

ADAPTIVE_XML = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background"/>
    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>
</adaptive-icon>
"""


class IconError(ConverterError, ValueError):
    """The executable's icon cannot be read."""


def dib_to_image(dib: bytes) -> raster.Image:
    """An RGBA image from a 32-bit icon DIB (a BITMAPINFOHEADER whose height counts the colour and the mask rows)."""
    size, width, height2, planes, bits, compression = struct.unpack_from("<IiiHHI", dib, 0)
    if size < 40 or bits != 32 or compression != 0 or planes != 1 or width <= 0 or height2 != 2 * width:
        raise IconError(f"not a 32-bit square icon DIB ({width}x{height2}, {bits} bits, compression {compression})")
    stride = width * 4
    pixels = dib[size : size + stride * width]
    if len(pixels) != stride * width:
        raise IconError("truncated icon DIB")
    rgba = bytearray(len(pixels))
    for y in range(width):  # rows are stored bottom up, pixels as BGRA
        row = pixels[(width - 1 - y) * stride : (width - y) * stride]
        out = rgba[y * stride : (y + 1) * stride]
        out[0::4], out[1::4], out[2::4], out[3::4] = row[2::4], row[1::4], row[0::4], row[3::4]
        rgba[y * stride : (y + 1) * stride] = out
    return raster.frombytes("RGBA", (width, width), bytes(rgba))


def read_icons(exe: PeImage, group: int = ICON_GROUP) -> dict[int, raster.Image]:
    """The 32-bit images of the executable's icon group by size."""
    data = exe.resource(RT_GROUP_ICON, group)
    reserved, kind, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or kind != 1:
        raise IconError("not an icon group")
    icons = {}
    for i in range(count):
        width, _height, _colours, _r, _planes, bits, _size, resource = struct.unpack_from("<BBBBHHIH", data, 6 + 14 * i)
        if bits == 32:
            icons[width or 256] = dib_to_image(exe.resource(RT_ICON, resource))
    if not icons:
        raise IconError(f"icon group {group} holds no 32-bit image")
    return icons


def resize(image: raster.Image, size: int) -> raster.Image:
    """`image` (RGBA, square) at `size` pixels: bilinear on premultiplied colour, pixel centres aligned."""
    if image.mode != "RGBA" or image.width != image.height:
        raise IconError("resize takes a square RGBA image")
    if size == image.width:
        return image
    n, data = image.width, image.data
    pre = [0.0] * (n * n * 4)  # premultiplied, 0..255
    for i in range(n * n):
        a = data[4 * i + 3]
        pre[4 * i] = data[4 * i] * a / 255
        pre[4 * i + 1] = data[4 * i + 1] * a / 255
        pre[4 * i + 2] = data[4 * i + 2] * a / 255
        pre[4 * i + 3] = a
    # for each output coordinate: the two source indices and the weight of the second
    taps = []
    for o in range(size):
        s = min(max((o + 0.5) * n / size - 0.5, 0.0), n - 1.0)
        i0 = int(s)
        taps.append((i0, min(i0 + 1, n - 1), s - i0))
    out = bytearray(size * size * 4)
    for y, (y0, y1, fy) in enumerate(taps):
        for x, (x0, x1, fx) in enumerate(taps):
            p00, p01, p10, p11 = 4 * (y0 * n + x0), 4 * (y0 * n + x1), 4 * (y1 * n + x0), 4 * (y1 * n + x1)
            w00, w01, w10, w11 = (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy
            a = pre[p00 + 3] * w00 + pre[p01 + 3] * w01 + pre[p10 + 3] * w10 + pre[p11 + 3] * w11
            o = 4 * (y * size + x)
            if a > 0:
                for c in range(3):
                    v = pre[p00 + c] * w00 + pre[p01 + c] * w01 + pre[p10 + c] * w10 + pre[p11 + c] * w11
                    out[o + c] = min(255, int(v * 255 / a + 0.5))
                out[o + 3] = min(255, int(a + 0.5))
    return raster.frombytes("RGBA", (size, size), bytes(out))


def flatten(image: raster.Image, ground: tuple[int, int, int] = BACKGROUND) -> raster.Image:
    """`image` over an opaque ground, as an RGB image."""
    rgb = bytearray(image.width * image.height * 3)
    for i in range(image.width * image.height):
        a = image.data[4 * i + 3]
        for c in range(3):
            rgb[3 * i + c] = (image.data[4 * i + c] * a + ground[c] * (255 - a) + 127) // 255
    return raster.frombytes("RGB", image.size, bytes(rgb))


def centred(image: raster.Image, canvas: int) -> raster.Image:
    """`image` in the middle of a transparent canvas of `canvas` pixels."""
    out = raster.new("RGBA", (canvas, canvas))
    out.paste(image, ((canvas - image.width) // 2, (canvas - image.height) // 2))
    return out


def encode_ico(images: list[raster.Image]) -> bytes:
    """An ICO of PNG images (Windows Vista on, and every browser)."""
    pngs = [raster.encode_png(i) for i in images]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for image, png in zip(images, pngs):
        out += struct.pack("<BBBBHHII", image.width % 256, image.height % 256, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    return bytes(out + b"".join(pngs))


def encode_icns(entries: list[tuple[bytes, bytes]]) -> bytes:
    """An Apple icon image from (OSType, PNG bytes) entries."""
    body = b"".join(kind + struct.pack(">I", 8 + len(png)) + png for kind, png in entries)
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


class IconSet:
    """The sizes made of the executable's icons, each once."""

    def __init__(self, icons: dict[int, raster.Image]):
        self.icons = icons
        self.largest = icons[max(icons)]
        self._made: dict[int, raster.Image] = {}

    def at(self, size: int) -> raster.Image:
        if size in self.icons:
            return self.icons[size]
        if size not in self._made:
            self._made[size] = resize(self.largest, size)
        return self._made[size]

    def png(self, size: int) -> bytes:
        return raster.encode_png(self.at(size))


def _write(path: Path, data: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data, str) else data)


def write_set(icons: dict[int, raster.Image], out: Path) -> list[Path]:
    """The platforms' icon files under `out`; returns the files written."""
    made = IconSet(icons)
    files: dict[str, bytes | str] = {}
    for size in icons:
        files[f"icon-{size}.png"] = made.png(size)
    files[f"icon-{WINDOW_ICON_SIZE}.png"] = made.png(WINDOW_ICON_SIZE)
    files["icon.ico"] = encode_ico([made.at(s) for s in ICO_SIZES])
    files["icon.icns"] = encode_icns([(kind, made.png(size)) for kind, size in ICNS_TYPES.items()])
    for name, size in IOS_ICONS.items():  # iOS takes no transparency
        files[f"ios/{name}.png"] = raster.encode_png(flatten(centred(made.at(round(size * IOS_FILL)), size)))
    files["web/favicon.ico"] = encode_ico([made.at(s) for s in FAVICON_ICO_SIZES])
    for name, size in WEB_ICONS.items():
        files[f"web/{name}.png"] = raster.encode_png(flatten(made.at(size)) if name.startswith("apple") else made.at(size))
    for density, scale in ANDROID_DENSITIES.items():
        legacy = round(ANDROID_LEGACY_DP * scale)
        canvas = round(ANDROID_ADAPTIVE_DP * scale)
        files[f"android/res/mipmap-{density}/ic_launcher.png"] = made.png(legacy)
        files[f"android/res/mipmap-{density}/ic_launcher_foreground.png"] = raster.encode_png(
            centred(made.at(round(canvas * ADAPTIVE_FILL)), canvas))
    files["android/res/mipmap-anydpi-v26/ic_launcher.xml"] = ADAPTIVE_XML
    files["android/res/values/ic_launcher_background.xml"] = (
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <color name="ic_launcher_background">'
        f"#{BACKGROUND[0]:02x}{BACKGROUND[1]:02x}{BACKGROUND[2]:02x}</color>\n</resources>\n")
    written = []
    for name, data in files.items():
        _write(out / name, data)
        written.append(out / name)
    return written


def convert(root: str | Path, profile: Profile, out: Path) -> list[Path]:
    """Read the profile's executable under `root` and write the icon files to `out`/icon."""
    root = Path(root)
    verify_file(root, profile.executable, full_hash=False)
    icons = read_icons(PeImage.load(root / profile.executable.name))
    written = write_set(icons, out / "icon")
    log.info("icon: %d files from the executable's %s pixel images", len(written), "/".join(map(str, sorted(icons))))
    return written
