"""Atlas pages of the SDL runtime's resource packs (docs/formats/packs.md): a deterministic
skyline packer and the 8-bit page images.

Every page is PAGE_SIZE x PAGE_SIZE, one byte per texel: the palette index, 0 transparent.
The runtime samples texels by integer coordinates, so frames need no padding, extrusion,
rotation or trimming. Same inputs give the same placements and the same page bytes.

Packing (`pack`): rectangles are sorted by height, then width (both descending), then by
their input position. Each one goes to the first page where it fits, at the skyline position
with the lowest top edge (ties: the lowest x); a rectangle that fits no page opens a new one.
"""
from __future__ import annotations

from dataclasses import dataclass

from .. import raster
from ..errors import ConverterError

PAGE_SIZE = 2048
ALGORITHM = b"SKY1"  # producer id of the packed sections (PACK FRMS, PAGE)
ALGORITHM_VERSION = 1


class PageError(ConverterError, ValueError):
    """A rectangle larger than a page."""


@dataclass(frozen=True)
class Placement:
    page: int
    x: int
    y: int


EMPTY = Placement(0, 0, 0)


class _Skyline:
    """Top edge of the occupied area of one page as segments [x, y, width], left to right."""

    def __init__(self, size: int):
        self.size = size
        self.segments = [[0, 0, size]]

    def find(self, w: int, h: int) -> tuple[int, int, int] | None:
        """(segment index, x, y) of the lowest top edge where a w x h rectangle fits, or None."""
        best, best_key = None, None
        segs, size = self.segments, self.size
        for i, (x, _, _) in enumerate(segs):
            if x + w > size:
                break
            y, rest, j = 0, w, i
            while rest > 0:
                y = max(y, segs[j][1])
                rest -= segs[j][2]
                j += 1
            if y + h > size:
                continue
            key = (y + h, x)
            if best_key is None or key < best_key:
                best, best_key = (i, x, y), key
        return best

    def place(self, i: int, x: int, y: int, w: int, h: int) -> None:
        segs = self.segments
        segs.insert(i, [x, y + h, w])
        end = x + w
        k = i + 1
        while k < len(segs) and segs[k][0] < end:
            cut = end - segs[k][0]
            if segs[k][2] <= cut:
                del segs[k]
                continue
            segs[k][0] += cut
            segs[k][2] -= cut
            break
        k = 0
        while k + 1 < len(segs):
            if segs[k][1] == segs[k + 1][1]:
                segs[k][2] += segs[k + 1][2]
                del segs[k + 1]
            else:
                k += 1


def pack(sizes: list[tuple[int, int]], page_size: int = PAGE_SIZE) -> tuple[list[Placement], int]:
    """Placements of (width, height) rectangles in input order, and the page count. A
    rectangle of zero width or height gets EMPTY and takes no space."""
    order = sorted((i for i, (w, h) in enumerate(sizes) if w and h), key=lambda i: (-sizes[i][1], -sizes[i][0], i))
    placements = [EMPTY] * len(sizes)
    pages: list[_Skyline] = []
    for i in order:
        w, h = sizes[i]
        if w > page_size or h > page_size:
            raise PageError(f"rectangle {w}x{h} larger than a {page_size}x{page_size} page")
        for p, sky in enumerate(pages):
            spot = sky.find(w, h)
            if spot:
                break
        else:
            pages.append(_Skyline(page_size))
            p, sky = len(pages) - 1, pages[-1]
            spot = sky.find(w, h)
        k, x, y = spot
        sky.place(k, x, y, w, h)
        placements[i] = Placement(p, x, y)
    return placements, len(pages)


def render(images: list[tuple[int, int, bytes]], placements: list[Placement], pages: int,
           page_size: int = PAGE_SIZE) -> list[bytes]:
    """Row-major texels of each page: image k (width, height, row-major indices) at placements[k];
    every other texel is 0."""
    out = [bytearray(page_size * page_size) for _ in range(pages)]
    for (w, h, pixels), at in zip(images, placements):
        if not (w and h):
            continue
        page = out[at.page]
        for row in range(h):
            o = (at.y + row) * page_size + at.x
            page[o : o + w] = pixels[row * w : (row + 1) * w]
    return [bytes(p) for p in out]


def png(texels: bytes, page_size: int = PAGE_SIZE) -> bytes:
    """An 8-bit grayscale PNG of one page."""
    return raster.encode_png(raster.frombytes("L", (page_size, page_size), texels))
