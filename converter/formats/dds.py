"""DirectDraw Surface images and StarCraft: Remastered's DDS containers (docs/research/remastered.md).

`decode` reads a DDS file into RGBA8 rows: uncompressed 32-bit (the SD glue and console art, masks R, G, B, A
in byte order) and the block formats DXT1 (BC1) and DXT5 (BC3). Remastered stores several images in one file as
`*.dds.grp` (one DDS per GRP frame) and `*.dds.vr4` (one per megatile): `u32` file size, `u16` count, `u16`
version, then per image `u32` 0, `u16` width, `u16` height, `u32` size and the DDS file.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from ..errors import ConverterError

MAGIC = b"DDS "
HEADER_SIZE = 128
PF_FOURCC = 0x4
PF_RGB = 0x40
CONTAINER_HEADER = struct.Struct("<IHH")
CONTAINER_ENTRY = struct.Struct("<IHHI")


class DdsError(ConverterError, ValueError):
    """Malformed or unsupported DDS data."""


@dataclass(frozen=True)
class Header:
    width: int
    height: int
    fourcc: bytes  # b"DXT1", b"DXT5", or b"" for uncompressed RGB(A)
    bits: int
    masks: tuple[int, int, int, int]  # R, G, B, A of an uncompressed image


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgba: bytes  # width * height * 4, rows top down


def header(data: bytes) -> Header:
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise DdsError("not a DDS file")
    height, width = struct.unpack_from("<II", data, 12)
    flags, fourcc, bits = struct.unpack_from("<I4sI", data, 80)
    masks = struct.unpack_from("<4I", data, 92)
    if flags & PF_FOURCC:
        return Header(width, height, fourcc, 0, (0, 0, 0, 0))
    if not flags & PF_RGB:
        raise DdsError(f"unsupported pixel format flags {flags:#x}")
    return Header(width, height, b"", bits, masks)


def _rgb565(c: int) -> tuple[int, int, int]:
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def _colour_block(block: bytes, four: bool) -> list[tuple[int, int, int, int]]:
    """The 16 RGBA texels of a BC1 colour block; `four` forces the four-colour mode (BC3)."""
    c0, c1, idx = struct.unpack_from("<HHI", block)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    if four or c0 > c1:
        palette = [(*p0, 255), (*p1, 255),
                   (*((2 * a + b) // 3 for a, b in zip(p0, p1)), 255),
                   (*((a + 2 * b) // 3 for a, b in zip(p0, p1)), 255)]
    else:
        palette = [(*p0, 255), (*p1, 255), (*((a + b) // 2 for a, b in zip(p0, p1)), 255), (0, 0, 0, 0)]
    return [palette[(idx >> (2 * i)) & 3] for i in range(16)]


def _alpha_block(block: bytes) -> list[int]:
    a0, a1 = block[0], block[1]
    bits = int.from_bytes(block[2:8], "little")
    if a0 > a1:
        table = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        table = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [table[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(data: bytes) -> Image:
    h = header(data)
    body = data[HEADER_SIZE:]
    w, ht = h.width, h.height
    out = bytearray(w * ht * 4)
    if h.fourcc in (b"DXT1", b"DXT5"):
        step = 8 if h.fourcc == b"DXT1" else 16
        bw, bh = (w + 3) // 4, (ht + 3) // 4
        if len(body) < bw * bh * step:
            raise DdsError(f"{h.fourcc.decode()} data truncated")
        for by in range(bh):
            for bx in range(bw):
                block = body[(by * bw + bx) * step:(by * bw + bx + 1) * step]
                if step == 8:
                    texels = _colour_block(block, False)
                else:
                    alpha = _alpha_block(block[:8])
                    texels = [(r, g, b, a) for (r, g, b, _), a in zip(_colour_block(block[8:], True), alpha)]
                for i, t in enumerate(texels):
                    x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                    if x < w and y < ht:
                        o = (y * w + x) * 4
                        out[o:o + 4] = bytes(t)
        return Image(w, ht, bytes(out))
    if h.fourcc:
        raise DdsError(f"unsupported DDS format {h.fourcc!r}")
    if h.bits != 32 or h.masks != (0xFF, 0xFF00, 0xFF0000, 0xFF000000):
        raise DdsError(f"unsupported uncompressed DDS: {h.bits} bits, masks {[hex(m) for m in h.masks]}")
    if len(body) < w * ht * 4:
        raise DdsError("pixel data truncated")
    return Image(w, ht, bytes(body[:w * ht * 4]))


def container(data: bytes) -> list[tuple[int, int, bytes]]:
    """The images of a `*.dds.grp` or `*.dds.vr4`: (width, height, DDS file) each."""
    if len(data) < CONTAINER_HEADER.size:
        raise DdsError("DDS container truncated")
    size, count, _version = CONTAINER_HEADER.unpack_from(data)
    if size != len(data):
        raise DdsError(f"DDS container of {len(data)} bytes records {size}")
    out, at = [], CONTAINER_HEADER.size
    for i in range(count):
        if at + CONTAINER_ENTRY.size > len(data):
            raise DdsError(f"DDS container entry {i} truncated")
        _, width, height, length = CONTAINER_ENTRY.unpack_from(data, at)
        at += CONTAINER_ENTRY.size
        if at + length > len(data):
            raise DdsError(f"DDS container image {i} truncated")
        out.append((width, height, data[at:at + length]))
        at += length
    return out
