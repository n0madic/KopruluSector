"""8-bit raster images and PNG files on the standard library (zlib, struct).

The converter's images are small and simple: luminance (L), luminance with alpha (LA), RGB,
RGBA and palette (P) images with 8 bits per channel. This module covers exactly that:
building images (new, frombytes, paste, putpalette), writing PNG (filter 0, no interlace)
and reading PNG of those colour types with any of the five row filters.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

from .errors import ConverterError

# mode -> (channels, PNG colour type)
MODES = {"L": (1, 0), "LA": (2, 4), "RGB": (3, 2), "RGBA": (4, 6), "P": (1, 3)}
_COLOUR_TYPES = {colour: mode for mode, (_, colour) in MODES.items()}
_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_COMPRESS_LEVEL = 6


class RasterError(ConverterError, ValueError):
    """An image argument or a PNG file this module does not handle."""


@dataclass
class Image:
    mode: str
    width: int
    height: int
    data: bytearray
    palette: bytes | None = None  # P: 3 bytes per entry

    @property
    def size(self) -> tuple[int, int]:
        return self.width, self.height

    @property
    def channels(self) -> int:
        return MODES[self.mode][0]

    def tobytes(self) -> bytes:
        return bytes(self.data)

    def getpixel(self, xy: tuple[int, int]) -> int | tuple[int, ...]:
        x, y = xy
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise RasterError(f"pixel {xy} outside {self.size}")
        n = self.channels
        at = (y * self.width + x) * n
        return self.data[at] if n == 1 else tuple(self.data[at : at + n])

    def paste(self, other: Image, xy: tuple[int, int]) -> None:
        """Copies `other` (same mode) with its top-left corner at xy, clipped to this image."""
        if other.mode != self.mode:
            raise RasterError(f"paste of a {other.mode} image into a {self.mode} image")
        n = self.channels
        x0, y0 = xy
        left, right = max(0, x0), min(self.width, x0 + other.width)
        if left >= right:
            return
        for y in range(max(0, y0), min(self.height, y0 + other.height)):
            src = ((y - y0) * other.width + left - x0) * n
            dst = (y * self.width + left) * n
            self.data[dst : dst + (right - left) * n] = other.data[src : src + (right - left) * n]

    def putpalette(self, values: list[int] | bytes) -> None:
        if self.mode != "P":
            raise RasterError("putpalette on a non-palette image")
        values = bytes(values)
        if len(values) % 3 or not 0 < len(values) <= 768:
            raise RasterError(f"palette of {len(values)} values")
        self.palette = values


def _check_mode(mode: str) -> int:
    if mode not in MODES:
        raise RasterError(f"unsupported mode {mode!r}")
    return MODES[mode][0]


def new(mode: str, size: tuple[int, int], fill: int | tuple[int, ...] = 0) -> Image:
    n = _check_mode(mode)
    width, height = size
    pixel = bytes([fill]) if isinstance(fill, int) else bytes(fill)
    if len(pixel) == 1 and n > 1:
        pixel = pixel * n
    if len(pixel) != n:
        raise RasterError(f"fill {fill!r} for mode {mode}")
    return Image(mode, width, height, bytearray(pixel * (width * height)))


def frombytes(mode: str, size: tuple[int, int], data: bytes) -> Image:
    n = _check_mode(mode)
    width, height = size
    if len(data) != width * height * n:
        raise RasterError(f"{len(data)} bytes for a {width}x{height} {mode} image")
    return Image(mode, width, height, bytearray(data))


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def encode_png(image: Image) -> bytes:
    """PNG bytes of `image`: 8-bit samples, filter type 0 on every row."""
    n, colour = MODES[image.mode]
    if image.width <= 0 or image.height <= 0:
        raise RasterError(f"empty image {image.size}")
    stride = image.width * n
    raw = bytearray()
    for y in range(image.height):
        raw.append(0)
        raw += image.data[y * stride : (y + 1) * stride]
    out = [_SIGNATURE, _chunk(b"IHDR", struct.pack(">IIBBBBB", image.width, image.height, 8, colour, 0, 0, 0))]
    if image.mode == "P":
        out.append(_chunk(b"PLTE", image.palette if image.palette is not None else bytes(768)))
    out.append(_chunk(b"IDAT", zlib.compress(bytes(raw), _COMPRESS_LEVEL)))
    out.append(_chunk(b"IEND", b""))
    return b"".join(out)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(raw: bytes, width: int, height: int, n: int) -> bytearray:
    stride = width * n
    if len(raw) != height * (stride + 1):
        raise RasterError("PNG image data has the wrong size")
    out = bytearray(height * stride)
    prev = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(n, stride):
                line[i] = (line[i] + line[i - n]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - n] if i >= n else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = line[i - n] if i >= n else 0
                upper_left = prev[i - n] if i >= n else 0
                line[i] = (line[i] + _paeth(left, prev[i], upper_left)) & 0xFF
        elif kind != 0:
            raise RasterError(f"PNG row filter {kind}")
        out[y * stride : (y + 1) * stride] = line
        prev = line
    return out


def _chunks(data: bytes):
    if not data.startswith(_SIGNATURE):
        raise RasterError("not a PNG file")
    pos = len(_SIGNATURE)
    while pos + 8 <= len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        if len(payload) != length:
            raise RasterError("truncated PNG chunk")
        yield kind, payload
        if kind == b"IEND":
            return
        pos += 12 + length
    raise RasterError("PNG without IEND")


def decode_png(data: bytes) -> Image:
    header, palette, idat = None, None, bytearray()
    for kind, payload in _chunks(data):
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"PLTE":
            palette = bytes(payload)
        elif kind == b"IDAT":
            idat += payload
    if header is None:
        raise RasterError("PNG without IHDR")
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or interlace != 0 or colour not in _COLOUR_TYPES:
        raise RasterError(f"PNG depth {depth}, colour type {colour}, interlace {interlace} is not handled")
    mode = _COLOUR_TYPES[colour]
    image = Image(mode, width, height, _unfilter(zlib.decompress(bytes(idat)), width, height, MODES[mode][0]))
    if mode == "P":
        image.palette = palette
    return image


def open_png(path: Path) -> Image:
    return decode_png(Path(path).read_bytes())


def png_size(path: Path) -> tuple[int, int]:
    """Width and height from a PNG's IHDR (any colour type or depth)."""
    with open(path, "rb") as f:
        head = f.read(24)
    if not head.startswith(_SIGNATURE) or head[12:16] != b"IHDR":
        raise RasterError(f"{path}: not a PNG file")
    return struct.unpack(">II", head[16:24])
