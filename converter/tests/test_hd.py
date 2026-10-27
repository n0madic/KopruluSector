"""HD import (converter/emit/hd.py, ADR 0013 stage R2) on synthetic data; with KOPRULU_SCR, a real install's HD2 is
imported and its packs and tiles checked against the original's frames and megatiles."""
from __future__ import annotations

import os
import struct
import tempfile
import unittest
from pathlib import Path

from converter.emit import hd
from converter.kpf.container import parse
from converter.tests.test_scr_rebuild import dds_dxt1

SCR = os.environ.get("KOPRULU_SCR")


class PackingTest(unittest.TestCase):
    def test_skyline_places_lowest_then_leftmost(self):
        line = hd.Skyline(4)  # 4 x 4 blocks
        line.place(2, 3, line.find(2, 3))
        assert line.line == [(0, 3, 2), (2, 0, 2)]
        assert line.find(2, 2) == (0, 2, 1)  # beside it, on the floor
        line.place(2, 2, line.find(2, 2))
        assert line.find(2, 1) == (2, 2, 1)  # the lower of the two tops
        assert line.find(3, 1) == (3, 0, 0)
        assert line.find(1, 4) is None

    def test_pack_groups_keeps_a_group_together(self):
        placed, pages = hd.pack_groups([[(8, 8), (4, 4)], [(16, 12)], [(4, 4)]], 16)
        assert pages == 2
        assert placed[0] == [(0, 0, 0), (0, 8, 0)]
        assert placed[1] == [(1, 0, 0)]  # no room left on the first page's floor: a new page
        assert placed[2] == [(0, 12, 0)]  # an earlier page still open takes the small one
        with self.assertRaises(hd.HdError):
            hd.pack_groups([[(32, 4)]], 16)

    def test_block_rect_widens_to_blocks(self):
        from converter.formats.anim import Frame
        assert hd.block_rect(Frame(5, 2, 0, 0, 6, 3)) == (4, 0, 8, 8)

    def test_blit_blocks_places_block_rows(self):
        page = bytearray(4 * 4 * 8)  # a 16 x 16 BC1 page: 4 x 4 blocks
        sheet = bytes(range(2 * 2 * 8))  # an 8 x 8 sheet: 2 x 2 blocks
        hd.blit_blocks(page, 16, 8, 4, 8, 8, sheet, 8)
        assert page[(1 * 4 + 2) * 8:(1 * 4 + 4) * 8] == sheet[:16]
        assert page[(2 * 4 + 2) * 8:(2 * 4 + 4) * 8] == sheet[16:]
        page = bytearray(4 * 4 * 8)
        hd.blit_blocks(page, 16, 0, 0, 4, 4, sheet, 8, 8, 4, 4)  # the sheet's last block alone
        assert page[:8] == sheet[24:] and not any(page[8:])

    def test_blocks_of_a_sheet(self):
        fourcc, data = hd.blocks(dds_dxt1(8, 4, bytes(range(16))))
        assert fourcc == b"DXT1" and data == bytes(range(16))


@unittest.skipIf(not SCR, "KOPRULU_SCR not set")
class RemasteredHdTest(unittest.TestCase):
    def test_hd2(self):  # hd (x4) is checked alike by `python3 -m converter hd`; it writes 1.9 GB
        from converter.casc.source import ScrSource

        with ScrSource(SCR) as s, tempfile.TemporaryDirectory() as tmp:
            counts = hd.generate(s, s.storage, Path(tmp), "hd2")
            assert counts["grps"] == 832, counts  # every GRP of 1.23.10.13515 has HD frames
            megatiles = {n: len(s.read(f"tileset\\{n}.vf4")[1]) // hd.MEGATILE_VF4 for n in counts["tiles"]}
            assert counts["tiles"] == megatiles
            for pack, (frames, pages) in counts["packs"].items():
                c = parse((Path(tmp) / "hd" / "hd2" / "packs" / f"{pack}.kpd").read_bytes())
                head = {sec.fourcc: sec.payload for sec in c.sections}[b"HEAD"]
                assert struct.unpack("<BHHIB", head) == (2, hd.PAGE_SIZE["hd2"], pages, frames, hd.FORMAT_BC)


if __name__ == "__main__":
    unittest.main()
