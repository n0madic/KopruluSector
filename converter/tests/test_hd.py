"""HD interface import (converter/emit/hd.py, hd_ui.py, hd_video.py; ADR 0013 stages R2 and R4d) on synthetic data;
with KOPRULU_SCR, a real install's HD2 interface and portraits imported (the HD world: tests/c/test_hd.c, ADR 0019)."""
from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from converter.emit import hd, hd_video
from converter.formats import dds
from converter.tests.test_scr_rebuild import dds_dxt1

SCR = os.environ.get("KOPRULU_SCR")


class BlocksTest(unittest.TestCase):
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
        assert all(hd_ui.sd_only(f"glue\\score{r}{k}\\untab.pcx") for r in "TZP" for k in "dv")
        assert not hd_ui.sd_only("glue\\scoreTd\\pMain.pcx") and not hd_ui.sd_only("glue\\scoreTd\\scorebox.pcx")

    def test_wing_tile_repeats_without_a_seam(self):
        from converter.emit import hd_ui
        from converter.formats.dds import Image as DdsImage
        s, x0, x1, h = 2, 10, 60, 4  # the part's 50 logical columns at scale 2
        w = 80 * s
        rgba = bytes(v for y in range(h) for x in range(w) for v in (x, y, 50, 255))
        image = DdsImage(w, h, rgba)
        for side in (0, 1):
            period, height, tile = hd_ui.wing_tile(image, (x0, x1), side, s)
            assert (period, height) == ((x1 - x0 - hd_ui.TILE_BLEND) * s, h) and len(tile) == period * h * 4
        # past the blend each column is the part's own, and the last column runs into the first (the image's gradient: 1)
        period, _, tile = hd_ui.wing_tile(image, (x0, x1), 0, s)
        assert tile[(period - 1) * 4] == x0 * s + period - 1
        assert abs(tile[0] - tile[(period - 1) * 4]) <= 4
        period, _, tile = hd_ui.wing_tile(image, (x0, x1), 1, s)  # mirrored: its far column is the part's first
        assert tile[0] == x1 * s - period
        assert abs(tile[0] - tile[(period - 1) * 4]) <= 4

    def test_tile_origin_lies_on_its_piece(self):
        from converter.emit import hd_ui
        columns = (270, 492)
        period = columns[1] - columns[0] - hd_ui.TILE_BLEND
        assert hd_ui.tile_origin(-106, columns, 0) == -106 + 492 - hd_ui.TILE_BLEND
        assert hd_ui.tile_origin(176, (0, 227), 1) + (227 - hd_ui.TILE_BLEND) == 176 + hd_ui.TILE_BLEND
        assert period > 0

    def test_art_cells_mark_the_fringe(self):
        from converter.emit import hd_ui
        s = hd_ui.PLAN_SCALE
        rgba = bytearray(4 * s * 3 * s * 4)  # 4 x 3 logical
        def paint(cx, cy, covered):
            for k, (dx, dy) in enumerate((a, b) for b in range(s) for a in range(s)):
                if k < covered:
                    o = ((cy * s + dy) * 4 * s + cx * s + dx) * 4
                    rgba[o:o + 4] = bytes((200, 10, 10, 255))
        paint(0, 0, 4)  # whole
        paint(1, 0, 1)  # a quarter: the HD art's edge
        paint(2, 0, 3)  # most of it: its pixel still
        cells = hd_ui.art_cells(4 * s, 3 * s, bytes(rgba), lambda c: 5 if c == (200, 10, 10) else 9)
        assert cells[0] == 5 and cells[1] == 5 | 0x80 and cells[2] == 5 and cells[3] == 0 and cells[4:] == bytes(8)

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

        self.assertIsNone(hd_video.clip(Empty(), "portrait/x/y.webm"))


@unittest.skipIf(not SCR, "KOPRULU_SCR not set")
class RemasteredHdTest(unittest.TestCase):
    def test_console_pieces(self):
        from converter.casc.source import ScrSource
        from converter.emit import hd_ui
        with ScrSource(SCR) as s:
            section = hd_ui.console_section(lambda p: s.read(p)[1], s.storage)
        assert section[0] == 3
        at, races = 1, {}
        for _ in range(section[0]):
            letter, colours = hd_ui.CONW_RACE.unpack_from(section, at)
            at += hd_ui.CONW_RACE.size
            pieces = []
            for _ in range(5):
                x, y, w, h, period = hd_ui.CONW_PIECE.unpack_from(section, at)
                at += hd_ui.CONW_PIECE.size
                cells = section[at:at + w * h]
                at += w * h
                assert any(cells) and max(c & 0x7f for c in cells) <= len(colours), (letter, x)
                pieces.append((x, y, w, h, period))
            races[letter] = pieces
        assert at == len(section) and set(races) == {b"z", b"t", b"p"}
        for letter, (left, centre, right, left_tile, right_tile) in races.items():
            # the ornaments (kp_layout.c's ORNAMENT_LEFT and ORNAMENT_RIGHT)
            assert left[0] == -106 and right[0] + right[2] == 640 + 107, letter
            assert centre[1] == 352 and left[4] == centre[4] == right[4] == 0, letter
            assert left_tile[4] == left_tile[2] > 0 and right_tile[4] == right_tile[2] > 0, letter
            # the first copies lie on their pieces' wings' ends
            assert left_tile[0] < left[0] + left[2] and right_tile[0] + right_tile[2] > right[0], letter

    def test_hd2(self):  # hd (x4) is checked alike by `python3 -m converter hd`
        from converter.casc.source import ScrSource

        with ScrSource(SCR) as s, tempfile.TemporaryDirectory() as tmp:
            counts = hd.generate(s, Path(tmp), "hd2")
            # the interface's GRPs and PCX with HD art and the consoles; the pictures Remastered drew anew stay SD
            assert counts["ui"] == {"assets": 60, "frames": 2180, "drawn_anew": 128, "moved": 62}, counts["ui"]
            assert counts["portraits"]["clips"] == counts["portraits"]["movies"]  # every portrait movie has HD2's


if __name__ == "__main__":
    unittest.main()
