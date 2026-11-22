#!/usr/bin/env python3
"""Compare the frames two runs of one script shot with the two graphics backends (ADR 0028, stage S9.5).

    gfx_compare.py GL_DIR GPU_DIR [--max-diff N]

Each PNG in GL_DIR (a script's `shot` frames, on the fixed clock) is compared with the PNG of the same name in GPU_DIR,
and each line reports the pixels that differ and the largest difference of a channel. Without --max-diff every frame
must be the same, byte for byte once decoded (the index path's frames: both backends map an index to its palette
colour). With --max-diff N a frame fails when a channel differs by more than N (the HD path's frames: filtering and
blending may round differently). A PNG in only one of the directories fails. Exit 0, or 1 on any failure.
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

log = logging.getLogger("gfx_compare")

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {2: 3, 6: 4}  # PNG colour type -> channels: RGB, RGBA (8 bits each)


@dataclass
class Image:
    width: int
    height: int
    channels: int
    pixels: bytes  # rows top-down, channels interleaved


@dataclass
class Difference:
    pixels: int  # all of them
    differ: int  # those with any channel different
    largest: int  # the largest difference of one channel
    at: tuple[int, int] | None  # a pixel with the largest difference (x, y from the top)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path: Path) -> Image:
    """An 8-bit RGB or RGBA PNG without interlacing, as the product's screenshots are written."""
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path}: not a PNG")
    pos, idat, header = len(PNG_SIGNATURE), [], None
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError(f"{path}: no IHDR")
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or colour not in CHANNELS or interlace:
        raise ValueError(f"{path}: depth {depth}, colour type {colour}, interlace {interlace}: not 8-bit RGB(A)")
    channels = CHANNELS[colour]
    stride = width * channels
    raw = zlib.decompress(b"".join(idat))
    if len(raw) != height * (stride + 1):
        raise ValueError(f"{path}: {len(raw)} bytes of rows, not {height * (stride + 1)}")
    out = bytearray(height * stride)
    prior = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(channels, stride):
                row[i] = (row[i] + row[i - channels]) & 0xFF
        elif kind == 2:
            row = bytearray((r + p) & 0xFF for r, p in zip(row, prior))
        elif kind == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + prior[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                upper_left = prior[i - channels] if i >= channels else 0
                row[i] = (row[i] + _paeth(left, prior[i], upper_left)) & 0xFF
        elif kind != 0:
            raise ValueError(f"{path}: row {y}: filter {kind}")
        out[y * stride:(y + 1) * stride] = row
        prior = row
    return Image(width, height, channels, bytes(out))


def compare(a: Image, b: Image) -> Difference:
    """The two frames' difference over their colour channels (RGB; an alpha channel is the window's, not compared)."""
    if (a.width, a.height) != (b.width, b.height):
        raise ValueError(f"sizes differ: {a.width}x{a.height} and {b.width}x{b.height}")
    differ, largest, at = 0, 0, None
    if a.channels == b.channels and a.pixels == b.pixels:
        return Difference(a.width * a.height, 0, 0, None)
    for k in range(a.width * a.height):
        pa, pb = a.pixels[k * a.channels:k * a.channels + 3], b.pixels[k * b.channels:k * b.channels + 3]
        if pa == pb:
            continue
        differ += 1
        d = max(abs(x - y) for x, y in zip(pa, pb))
        if d > largest:
            largest, at = d, (k % a.width, k // a.width)
    return Difference(a.width * a.height, differ, largest, at)


def _colour(image: Image, at: tuple[int, int]) -> str:
    k = (at[1] * image.width + at[0]) * image.channels
    return image.pixels[k:k + 3].hex()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("gl", type=Path, help="the --gfx gl run's directory")
    ap.add_argument("gpu", type=Path, help="the --gfx gpu run's directory")
    ap.add_argument("--max-diff", type=int, default=None, help="the largest channel difference a frame may have")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    names = sorted({p.name for p in a.gl.glob("*.png")} | {p.name for p in a.gpu.glob("*.png")})
    if not names:
        log.error("gfx compare: no frames in %s or %s", a.gl, a.gpu)
        return 1
    failures = 0
    for name in names:
        if not (a.gl / name).is_file() or not (a.gpu / name).is_file():
            log.error("gfx compare %s: only in %s FAILED", name, a.gl if (a.gl / name).is_file() else a.gpu)
            failures += 1
            continue
        gl, gpu = read_png(a.gl / name), read_png(a.gpu / name)
        d = compare(gl, gpu)
        line = f"gfx compare {name}: {d.pixels} pixels, {d.differ} differ, largest channel difference {d.largest}"
        if d.at is not None:
            line += f" at {d.at[0]},{d.at[1]} (gl {_colour(gl, d.at)}, gpu {_colour(gpu, d.at)})"
        if d.largest > (a.max_diff or 0):
            line += " FAILED"
            failures += 1
        log.info("%s", line)
    log.info("gfx compare: %d frames, %d failed", len(names), failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
