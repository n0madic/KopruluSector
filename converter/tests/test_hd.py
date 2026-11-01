"""HD import (converter/emit/hd.py, ADR 0013 stage R2) on synthetic data; with KOPRULU_SCR, a real install's HD2 is
imported and its packs and tiles checked against the original's frames and megatiles."""
from __future__ import annotations

import os
import struct
import tempfile
import unittest
from pathlib import Path

from converter.emit import hd, hd_video
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

    def test_bc1_red_reads_both_modes(self):
        white, black = 0xF800, 0x0000
        opaque = struct.pack("<HHI", white, black, 0b11_10_01_00)  # c0 > c1: four colours
        cut = struct.pack("<HHI", black, white, 0b11_10_01_00)  # c0 <= c1: three and transparent black
        red = hd.bc1_red(opaque + cut, 8, 4)
        assert list(red[0:4]) == [255, 0, 170, 85]
        assert list(red[4:8]) == [0, 255, 127, 0]

    def test_creep_edges_in_their_tiles(self):
        from converter.formats import dds, grp

        o = grp.OPAQUE
        sd = grp.encode(32, 32, [grp.Frame(0, 22, 32, 10, bytes([1]) * 320, bytes([o]) * 320),
                                 grp.Frame(20, 0, 12, 32, bytes([1]) * 384, bytes([o]) * 384)])
        cut = struct.pack("<HHI", 0x0000, 0xF800, 0xFFFFFFFF)  # c0 <= c1, every texel index 3: transparent black
        red = struct.pack("<HHI", 0xF800, 0x0000, 0)  # every texel c0: red
        images = [dds_dxt1(64, 24, red * 16 * 6), dds_dxt1(24, 64, cut * 6 * 16)]  # the first 2 rows taller than SD's
        body = b"".join(struct.pack("<IHHI", 0, dds.header(d).width, dds.header(d).height, len(d)) + d for d in images)
        storage = type("Storage", (), {"read": lambda self, name: struct.pack("<IHH", 8 + len(body), 2, 0) + body})()
        payload = hd.creep_edges(storage, "HD2/", "badlands", sd, 64)
        assert struct.unpack_from("<H", payload) == (2,) and len(payload) == 2 + 2 * 64 * 64 * 4
        tile = payload[2:2 + 64 * 64 * 4]
        texel = lambda t, x, y: payload[2 + t * 64 * 64 * 4 + (y * 64 + x) * 4:][:4]
        assert texel(0, 0, 39) == bytes(4) and texel(0, 0, 40) == bytes([255, 0, 0, 255])  # moved up from 44 to fit
        assert texel(0, 63, 63) == bytes([255, 0, 0, 255]) and not any(tile[:40 * 64 * 4])
        assert not any(payload[2 + 64 * 64 * 4:])  # the second: transparent texels at (40, 0)
        with self.assertRaises(hd.HdError):
            hd.creep_edges(storage, "HD2/", "badlands", sd, 32)  # an HD frame larger than the tiles

    def test_blocks_of_a_sheet(self):
        fourcc, data = hd.blocks(dds_dxt1(8, 4, bytes(range(16))))
        assert fourcc == b"DXT1" and data == bytes(range(16))


class HdUiTest(unittest.TestCase):
    def test_hd_paths(self):
        from converter.emit import hd_ui
        assert hd_ui.hd_path("unit\\cmdbtns\\cmdicons.grp") == "unit/cmdicons/cmdicons.dds.grp"
        assert hd_ui.hd_path("unit\\cmdbtns\\pcmdbtns.grp") == "game/consoles/protoss/cmdbtns.dds.grp"
        assert hd_ui.hd_path("dlgs\\zerg.grp") == "game/consoles/zerg/dlg.dds.grp"
        assert hd_ui.hd_path("dlgs\\tile.grp") == "dlgs/tile.dds.grp"
        assert hd_ui.hd_path("glue\\PalNl\\Backgnd.pcx") == "glue/palnl/backgnd.dds"
        assert hd_ui.hd_path("rez\\gluexpcmpgn.bin") is None
        assert hd_ui.hd_path("game\\tpbrempt.pcx") == "game/consoles/terran/pbrempt.dds"
        assert hd_ui.hd_path("game\\ppbrfull.pcx") == "game/consoles/protoss/pbrfull.dds"
        assert hd_ui.console_picture("game\\zpbrfull.pcx") and not hd_ui.console_picture("game\\tconsole.pcx")

    def test_place_piece_finds_the_offset(self):
        from converter.emit import hd_ui
        from converter.formats.dds import Image as DdsImage
        w, h = 120, 40
        sd = bytearray(w * h * 4)
        for y in range(h):
            for x in range(w):
                o = (y * w + x) * 4
                sd[o:o + 4] = bytes((2 * x, 6 * y, 120 + x - y, 255))  # smooth, as console art is
        sd_image = DdsImage(w, h, bytes(sd))
        # a piece cut from the console at (15, 7), 90 x 30, with a transparent band where it does not match
        texels = []
        for py in range(30):
            for px in range(90):
                o = ((py + 7) * w + px + 15) * 4
                texels.append(tuple(sd[o:o + 4]) if px < 80 else (0, 0, 0, 0))
        assert hd_ui.place_piece(sd_image, (90, 30, texels), (15, 95), 10, 7) == 15

    def test_same_picture(self):
        from converter.emit import hd_ui
        from converter.formats.dds import Image as DdsImage
        from converter.formats.pcx import Pcx
        w, h = 40, 30
        palette = tuple((v, v, v) for v in range(256))
        pixels = bytes((x * 5 + (y // 6) * 40) % 256 for y in range(h) for x in range(w))  # ramps and steps
        sd = Pcx(w, h, pixels, palette)

        def hd(index):  # twice the size, index(x, y) of each logical pixel
            return DdsImage(2 * w, 2 * h, bytes(c for y in range(2 * h) for x in range(2 * w)
                                                for c in (index(x // 2, y // 2),) * 3 + (255,)))

        assert hd_ui.same_picture(sd, hd(lambda x, y: pixels[y * w + x]), 2) > 0.99
        assert hd_ui.same_picture(sd, hd(lambda x, y: pixels[y * w + w - 1 - x]), 2) < hd_ui.SAME_PICTURE  # mirrored

    def test_place_frame(self):
        from converter.emit import hd_ui

        def image(w, h, opaque):  # (width, height, alpha): opaque(x, y) of each texel
            return w, h, bytes(255 * opaque(x, y) for y in range(h) for x in range(w))

        def frame(x, y, w, h, opaque):
            return x, y, w, h, bytes(1 if opaque(i, j) else 0 for j in range(h) for i in range(w))

        full = lambda x, y: True  # noqa: E731
        # the minimap's blink: a 4 x 4 frame at (5, 5) whose HD art keeps the canvas from its origin, the content last
        blink = image(18, 18, lambda x, y: x >= 10 and y >= 10)
        assert hd_ui.place_frame(frame(5, 5, 4, 4, full), (16, 16), blink, 2) == (-10, -10, True)
        # a trimmed frame centred on the SD frame, a stray SD pixel aside (it does not pull the art)
        stray = frame(0, 0, 6, 6, lambda x, y: (x, y) == (0, 0) or 1 <= x < 5 and 1 <= y < 5)
        assert hd_ui.place_frame(stray, (8, 8), image(8, 8, full), 2) == (2, 2, False)
        # a wireframe: its layers side by side, centred on the GRP canvas, not on its 4 x 4 frame at (2, 2)
        wire = image(hd_ui.WIRE_LAYERS * 16, 16, lambda x, y: 4 <= x % 16 < 12 and 4 <= y < 12)
        assert hd_ui.place_frame(frame(2, 2, 4, 4, full), (8, 8), wire, 2) == (-4, -4, False)
        # a placeholder wireframe's text centred on its 4 x 2 frame at (0, 1), not where the canvas would put it
        text = image(hd_ui.WIRE_LAYERS * 16, 16, lambda x, y: 4 <= x % 16 < 12 and 6 <= y < 10)
        assert hd_ui.place_frame(frame(0, 1, 4, 2, full), (8, 8), text, 2) == (-4, -6, True)
        # an empty HD frame keeps its usual place (and is no wireframe)
        assert hd_ui.place_frame(frame(0, 0, 4, 4, full), (8, 8), (0, 0, b""), 2) == (4, 4, False)


class HdVideoTest(unittest.TestCase):
    def test_webm_name(self):
        self.assertEqual(hd_video.webm_name("portrait\\TMarine\\TMaTlk00.smk"), "portrait/tmarine/tmatlk00.webm")

    def test_clip_without_hd_art(self):
        class Empty:
            def has(self, name):
                return False

        self.assertIsNone(hd_video.clip(Empty(), b"", "portrait/x/y.webm", None))


@unittest.skipIf(not SCR, "KOPRULU_SCR not set")
class RemasteredHdTest(unittest.TestCase):
    def test_hd2(self):  # hd (x4) is checked alike by `python3 -m converter hd`; it writes 1.9 GB
        from converter.casc.source import ScrSource

        with ScrSource(SCR) as s, tempfile.TemporaryDirectory() as tmp:
            counts = hd.generate(s, s.storage, Path(tmp), "hd2")
            assert counts["grps"] == 832, counts  # every GRP of 1.23.10.13515 has HD frames
            megatiles = {n: len(s.read(f"tileset\\{n}.vf4")[1]) // hd.MEGATILE_VF4 for n in counts["tiles"]}
            assert counts["tiles"] == megatiles
            assert counts["normals"] == (45, 120)  # the water's normal maps' frames
            # the interface's GRPs and PCX with HD art and the consoles; the pictures Remastered drew anew stay SD
            assert counts["ui"] == {"assets": 66, "frames": 2180, "drawn_anew": 128, "moved": 62}, counts["ui"]
            assert counts["portraits"]["clips"] == counts["portraits"]["movies"]  # every portrait movie has HD2's
            jungle = {sec.fourcc: sec.payload for sec in parse((Path(tmp) / "hd" / "hd2" / "tilesets" / "jungle.kpd")
                                                               .read_bytes()).sections}
            wmap = struct.unpack(f"<{megatiles['jungle']}H", jungle[b"WMAP"])
            assert max(wmap) == len(jungle[b"WMSK"]) // (64 * 64) and wmap[49] > 0  # megatile 49: water
            for pack, (frames, pages) in counts["packs"].items():
                c = parse((Path(tmp) / "hd" / "hd2" / "packs" / f"{pack}.kpd").read_bytes())
                head = {sec.fourcc: sec.payload for sec in c.sections}[b"HEAD"]
                assert struct.unpack("<BHHIB", head) == (2, hd.PAGE_SIZE["hd2"], pages, frames, hd.FORMAT_BC)


if __name__ == "__main__":
    unittest.main()
