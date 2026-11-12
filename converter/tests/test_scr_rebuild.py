"""Remastered's own formats and the rebuilds of 1.17 files from them (ADR 0013, stage R1; ADR 0018, stage D6): KPW1,
DDS and converter/casc/rebuild.py on synthetic data."""
from __future__ import annotations

import shutil
import struct
import unittest

from converter.casc import rebuild
from converter.formats import dds, dialog, grp, kpw, palette, pcx, smk
from converter.kpf import fixtures
from converter.kpf.fixtures import dialog_bin

PALETTE = tuple((i, i, i) for i in range(256))


def dds_rgba(width: int, height: int, rgba: bytes) -> bytes:
    header = bytearray(dds.HEADER_SIZE)
    header[:4] = dds.MAGIC
    struct.pack_into("<II", header, 12, height, width)
    struct.pack_into("<I4sI4I", header, 80, dds.PF_RGB | 1, b"\0\0\0\0", 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    return bytes(header) + rgba


def dds_dxt1(width: int, height: int, blocks: bytes) -> bytes:
    header = bytearray(dds.HEADER_SIZE)
    header[:4] = dds.MAGIC
    struct.pack_into("<II", header, 12, height, width)
    struct.pack_into("<I4s", header, 80, dds.PF_FOURCC, b"DXT1")
    return bytes(header) + blocks


class KpwTest(unittest.TestCase):
    def test_header_reads_as_an_smk(self):
        data = kpw.encode(3, 2, 3, -6667, True, bytes(range(256)) * 3, b"webm")
        assert kpw.header(data) == (3, 2, 3, -6667, True) and data[kpw.HEADER.size + 768:] == b"webm"
        h = smk.header(data)
        assert (h.signature, h.width, h.height, h.frames, h.ring_frame, h.decoded_frames) == (b"KPW1", 3, 2, 3, True, 4)
        assert abs(h.ms_per_frame - 66.67) < 1e-9

    def test_rejects_malformed(self):
        with self.assertRaises(kpw.KpwError):
            kpw.encode(2, 2, 1, 100, False, bytes(767), b"")
        with self.assertRaises(kpw.KpwError):
            kpw.header(b"KPW1" + bytes(20 + 767))  # its palette cut short
        with self.assertRaises(kpw.KpwError):
            kpw.header(b"KPV1" + bytes(20 + 768))

    @unittest.skipIf(shutil.which("ffmpeg") is None, "no ffmpeg")
    def test_fixture_movies_decode_to_their_indices(self):
        movies = fixtures.kpw_fixture()
        if not movies:
            self.skipTest("FFmpeg without libvpx")
        for name in ("movie.kpw", "movie-alpha.kpw"):
            h, decoded = smk.decode(movies[name])
            assert b"".join(f.indices for f in decoded) == movies[name + ".indices"], name
            assert len(decoded) == h.decoded_frames and decoded[0].palette == movies[name][24:24 + 768], name


class DdsTest(unittest.TestCase):
    def test_uncompressed_and_container(self):
        image = dds_rgba(2, 1, bytes([1, 2, 3, 255, 4, 5, 6, 0]))
        decoded = dds.decode(image)
        assert (decoded.width, decoded.height, decoded.rgba) == (2, 1, bytes([1, 2, 3, 255, 4, 5, 6, 0]))
        body = struct.pack("<IHHI", 0, 2, 1, len(image)) + image
        container = struct.pack("<IHH", 8 + 2 * len(body), 2, 0x1001) + body * 2
        assert [(w, h) for w, h, _ in dds.container(container)] == [(2, 1), (2, 1)]

    def test_dxt1_block(self):
        # colour 0 white, colour 1 black, four-colour mode; texel indices 0, 1, 2, 3 on the first row, then 0
        block = struct.pack("<HHI", 0xFFFF, 0x0000, 0b11100100)
        decoded = dds.decode(dds_dxt1(4, 4, block)).rgba
        assert decoded[:16] == bytes([255, 255, 255, 255, 0, 0, 0, 255, 170, 170, 170, 255, 85, 85, 85, 255])
        assert decoded[16:20] == bytes([255, 255, 255, 255])

    def test_alpha_as_decoded(self):
        four = struct.pack("<HHI", 0xFFFF, 0x0000, 0xFFFFFFFF)  # four-colour mode: index 3 opaque
        three = struct.pack("<HHI", 0x0000, 0xFFFF, 0b11100100)  # three-colour mode: index 3 transparent
        for blocks, w, h in ((four + three, 8, 4), (three, 3, 2)):
            image = dds_dxt1(w, h, blocks)
            assert dds.alpha(image) == (w, h, dds.decode(image).rgba[3::4])
        image = dds_rgba(2, 1, bytes([1, 2, 3, 255, 4, 5, 6, 0]))
        assert dds.alpha(image) == (2, 1, bytes([255, 0]))


class RebuildTest(unittest.TestCase):
    def test_index_rule(self):
        colours = ((0, 0, 0), (9, 9, 9), (9, 9, 9), (200, 0, 0)) + ((255, 255, 255),) * 252
        index = palette.IndexMap(colours)
        assert index(9, 9, 9, 0) == 0  # transparent
        assert index(9, 9, 9, 255) == 1  # opaque: the lowest duplicate
        assert index(9, 9, 9, 170) == 2  # translucent: the highest
        assert index(190, 10, 10, 255) == 3  # not in the palette: the nearest

    def test_pcx_from_a_shorter_image_and_its_header(self):
        rgba = bytes([5, 5, 5, 255]) * 3
        header = bytes([1, 2, 3, 4, 1, 0])
        data = rebuild.pcx_from_dds(dds_rgba(3, 1, rgba), (3, 2), PALETTE, header)
        image = pcx.decode(data)
        assert image.pixels == bytes([0, 0, 0, 5, 5, 5]) and image.palette == PALETTE
        assert data[12:16] == header[:4] and data[68:70] == header[4:]
        assert rebuild.describe_pcx(data)["header"] == header.hex()

    def test_fit_modes(self):
        assert rebuild.fit_mode(4, 2, 2, 1) == rebuild.FIT_SAMPLE and rebuild.fits(4, 2, 2, 1)
        assert rebuild.fit_mode(4, 4, 3, 3) == rebuild.FIT_CROP  # block padding cut
        assert rebuild.fit_mode(640, 112, 640, 480) == rebuild.FIT_CROP  # a console's rows
        assert rebuild.fit_mode(9, 1, 3, 1) is None and not rebuild.fits(9, 1, 3, 1)

    def test_fit_cuts_block_padding_and_samples_hd(self):
        padded = dds.Image(4, 4, bytes(range(64)))
        fitted, above = rebuild.fit(padded, 3, 3)
        assert (fitted.width, fitted.height, above) == (3, 3, 0) and fitted.rgba[:12] == bytes(range(12))
        hd = dds.Image(4, 2, bytes(range(32)))
        fitted, _ = rebuild.fit(hd, 2, 1)
        assert fitted.rgba == bytes(range(0, 4)) + bytes(range(8, 12))
        with self.assertRaises(dds.DdsError):
            rebuild.fit(dds.Image(9, 1, bytes(36)), 3, 1)

    def test_tbl_spans_keep_inner_nuls(self):
        spans = [b"u\0Speed Up", b"", b"View Players"]
        data = rebuild.tbl_encode(spans)
        assert rebuild.tbl_spans(data) == spans
        strings = [("STR_SPEED", b"u\x07Speed Up"), ("STR_NEW", b"New"), ("STR_VIEW", b"View  Players")]
        ids = rebuild.align_ids(spans, strings)
        assert ids == ["STR_SPEED", "STR_NEW", "STR_VIEW"]
        rebuilt = rebuild.tbl_from_remastered(ids, {0: [1]}, strings)
        assert rebuild.tbl_spans(rebuilt) == [b"u\0Speed Up", b"New", b"View  Players"]
        assert rebuild.tbl_from_remastered(["A", "B"], {}, [("B", b"b")], base=[b"kept"]) == rebuild.tbl_encode(
            [b"kept", b"b"])

    def test_xml_strings_and_rc3_escapes(self):
        raw = (b"<strings><string><id>a</id><value>x &amp; y\xe2\x80\x99s</value></string>"
               b"<string><id>b</id><value>o\\\\4O\\\\1k</value></string></strings>")
        strings = rebuild.xml_strings(raw)
        assert strings[0] == ("a", b"x & y\x92s")
        assert rebuild.unescape_rc3(strings[1][1]) == b"o\x04O\x01k"
        assert rebuild.unescape_rc3(b"Won!\\\\nYou\\\\4\\tA\\nB") == b"Won!\nYou\x04\tA\nB"

    def test_dialog_skeleton_round_trip_and_relocation(self):
        data = dialog_bin()
        strings = rebuild.dialog_strings(data)
        assert [(o, n, hotkey, name) for o, n, _, hotkey, name in strings] == [(300, 4, True, False),
                                                                             (310, 8, False, True)]
        skeleton = rebuild.dialog_skeleton(data)
        assert b"Quit" not in skeleton and b"port.smk" not in skeleton
        texts = [(rebuild.DialogText(o, n, tuple(p), "", h), data[o:o + n]) for o, n, p, h, _ in strings]
        assert rebuild.dialog_rebuild(skeleton, texts) == data
        longer = [(t, b"quit now" if t.offset == 300 else text) for t, text in texts]
        widgets = dialog.parse(rebuild.dialog_rebuild(skeleton, longer))
        assert widgets[1].text == b"quit now"  # stored "Quit now", lowercased by the hotkey rule
        table = rebuild.dialog_table(data, [("x-1", b"quit")], {})
        assert [t[4] for t in table["texts"]] == [f"id:{rebuild.RC3}#x-1", "name:port.smk"]
        assert rebuild.dialog_from_table(table, lambda source: b"quit") == data

    def test_uncompressed_grp_and_remap(self):
        layout = {"box": [4, 4], "frames": [[0, 0, 2, 2, 30], [1, 1, 2, 2, 30], [0, 0, 2, 1, 34]]}
        data = rebuild.uncompressed_grp(layout, [b"\1\2\3\4", b"", b"\5\6"], 36)
        assert data[30:36] == b"\1\2\3\4\5\6"
        assert rebuild.uncompressed_grp_layout(data) == layout
        frame = grp.Frame(0, 0, 2, 1, bytes([3, 0]), bytes([grp.OPAQUE, grp.TRANSPARENT]))
        wpe = bytes([9, 9, 9, 0]) * 256
        out = grp.decode(rebuild.remap_grp(grp.Grp(2, 1, (frame,)), wpe, PALETTE))
        assert out.frames[0].indices == bytes([9, 0]) and out.frames[0].mask == frame.mask


if __name__ == "__main__":
    unittest.main()
