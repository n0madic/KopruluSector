"""Minimap terrain prerender: native layout, pixel choice and palette map."""
from __future__ import annotations

import struct
import unittest

from converter.emit import minimap
from converter.formats import tileset


def _tileset(megatiles: int) -> tileset.Tileset:
    """Megatile m uses minitile 4m + cell for cell 0..15; minitile k has byte 0x37 = k % 256."""
    vr4 = bytearray()
    for k in range(megatiles * 16):
        tile = bytearray(64)
        tile[minimap.PIXEL] = k % 256
        vr4 += tile
    vx4 = b"".join(struct.pack("<16H", *(((16 * m + c) << 1) | (c & 1) for c in range(16))) for m in range(megatiles))
    return tileset.decode(bytes(52), vx4, bytes(vr4), bytes(32 * megatiles))


IDENTITY = list(range(256))


class MinimapTest(unittest.TestCase):
    def test_layouts(self):
        ts = _tileset(4)
        w, h, px = minimap.render([1, 2, 3, 0x8000] * (64 * 64 // 4), 64, 64, ts, IDENTITY)
        assert (w, h) == (128, 128)
        assert px[0:2] == bytes([16, 17]) and px[128:130] == bytes([20, 21])  # minitiles 0, 1 / 4, 5 of megatile 1
        w, h, px = minimap.render([2] * (128 * 96), 128, 96, ts, IDENTITY)
        assert (w, h, px[0]) == (128, 96, 32)  # minitile 0 of megatile 2
        ids = [x % 4 for x in range(192 * 64)]
        w, h, px = minimap.render(ids, 192, 64, ts, IDENTITY)
        assert (w, h) == (96, 32)
        assert px[1] == 16 * ids[2]  # every second tile
        with self.assertRaises(ValueError):
            minimap.render([0] * 100, 10, 10, ts, IDENTITY)

    def test_palette_map(self):
        rgb = [(i, i, i) for i in range(256)]
        rgb[5] = (200, 200, 200)
        colours = minimap.palette_map(rgb, cycle={5, 200, 201})
        assert colours[4] == 4 and colours[5] == 199  # nearest static colour; 200/201 are cycling
        assert minimap.palette_map(rgb, cycle=set(range(1, 256)))[3] == 0  # no static candidate
