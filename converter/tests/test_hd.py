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
    def test_shelves_fill_rows_then_pages(self):
        at, pages = hd.shelves([(8, 8), (4, 4), (8, 4), (8, 8)], 16)
        assert pages == 1
        assert at[0] == (0, 0, 0) and at[3] == (0, 8, 0)  # the tallest first, side by side
        assert at[2] == (0, 0, 8) and at[1] == (0, 8, 8)  # the next shelf
        at, pages = hd.shelves([(16, 12), (16, 8)], 16)
        assert pages == 2 and at[1] == (1, 0, 0)
        with self.assertRaises(hd.HdError):
            hd.shelves([(32, 4)], 16)

    def test_blit_blocks_places_block_rows(self):
        page = bytearray(4 * 4 * 8)  # a 16 x 16 BC1 page: 4 x 4 blocks
        sheet = bytes(range(2 * 2 * 8))  # an 8 x 8 sheet: 2 x 2 blocks
        hd.blit_blocks(page, 16, 8, 4, 8, 8, sheet, 8)
        assert page[(1 * 4 + 2) * 8:(1 * 4 + 4) * 8] == sheet[:16]
        assert page[(2 * 4 + 2) * 8:(2 * 4 + 4) * 8] == sheet[16:]

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
                assert struct.unpack("<BHHI", head) == (2, hd.PAGE_SIZE["hd2"], pages, frames)


if __name__ == "__main__":
    unittest.main()
