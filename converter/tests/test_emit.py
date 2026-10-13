"""Emitters used by the phase-0 spikes (no original data needed)."""
from __future__ import annotations

import struct
import unittest

from converter.emit import atlas, sound, tilemap
from converter.emit.defold import ROOT, project_path
from converter.formats import grp, tileset
from converter.kpf.container import parse


def _grp() -> grp.Grp:
    # 2x1 frame at (1, 1) in a 4x3 canvas: opaque 7, transparent.
    return grp.Grp(4, 3, (grp.Frame(1, 1, 2, 1, bytes([7, 0]), bytes([255, 0])),
                          grp.Frame(0, 0, 0, 0, b"", b"")))


ENCODINGS = [
    (atlas.Encoding.LA, "LA", [(7, 255), (0, 0)]),
    (atlas.Encoding.INDEX, "L", [7, 0]),
    (atlas.Encoding.MASK, "L", [255, 0]),
]


class EmitTest(unittest.TestCase):
    def test_frame_image_encodings(self):
        for encoding, mode, crop in ENCODINGS:
            with self.subTest(encoding=encoding):
                g = _grp()
                img = atlas.frame_image(g.frames[0], g, atlas.Layout.CROP, encoding)
                assert img.mode == mode and img.size == (2, 1)
                assert [img.getpixel((x, 0)) for x in range(2)] == crop
                canvas = atlas.frame_image(g.frames[0], g, atlas.Layout.CANVAS, encoding)
                assert canvas.size == (4, 3) and canvas.getpixel((1, 1)) == crop[0]
                empty = atlas.frame_image(g.frames[1], g, atlas.Layout.CROP, encoding)
                assert empty.size == (1, 1)

    def test_index_encoding_rejects_opaque_zero(self):
        g = grp.Grp(1, 1, (grp.Frame(0, 0, 1, 1, b"\0", b"\xff"),))
        with self.assertRaises(atlas.OpaqueZeroError):
            atlas.frame_image(g.frames[0], g, atlas.Layout.CROP, atlas.Encoding.INDEX)

    def test_atlas_text_and_ids(self):
        spec = atlas.AtlasSpec([atlas.Animation("terran_marine", ["/a/000.png", "/a/001.png"])], max_page_size=1024)
        text = spec.to_text()
        assert text.count("images {") == 2 and 'id: "terran_marine"' in text
        assert "max_page_width: 1024" in text and "PLAYBACK_NONE" in text
        assert atlas.animation_id("unit\\Terran\\marine.grp") == "terran_marine"
        profiles = atlas.texture_profiles([("/x/**", "l8")])
        assert profiles.index('"/x/**"') < profiles.index('path: "**"')
        assert "TEXTURE_FORMAT_LUMINANCE\n" in profiles and "premultiply_alpha: false" in profiles

    def test_project_path(self):
        assert project_path(ROOT / "generated" / "x.png") == "/generated/x.png"

    def test_tilemap_templates_and_mtxm_helpers(self):
        corners = tilemap.tilemap_text("/t.tilesource", "/m.material", full=False)
        assert corners.count("cell {") == 2 and "x: 255\n    y: 255" in corners
        mtxm = struct.pack("<6H", 1, 2, 3, 4, 5, 6)  # 3x2
        assert tilemap.crop_mtxm(mtxm, 3, 2, 1) == struct.pack("<2H", 1, 2)
        big = tilemap.tile_mtxm(mtxm, 3, 2, 4)
        assert struct.unpack("<16H", big)[:8] == (1, 2, 3, 1, 4, 5, 6, 4)

    def test_tileset_and_mission_containers(self):
        cv5 = bytearray(tileset.CV5_RECORD)
        struct.pack_into("<16H", cv5, tileset.CV5_HEADER, *range(16))
        ts = tileset.decode(bytes(cv5), bytes(32), bytes(64), bytes(32))
        c = parse(tilemap.tileset_container("desert", ts, bytes(1024), b"\1" * 32))
        assert c.kind == b"TSET" and struct.unpack("<16H", c.section("CV5M").payload) == tuple(range(16))
        assert c.section("CYCL").payload == bytes([2, 8, 1, 13, 248, 254])
        m = parse(tilemap.mission_container(3, 2, 5, bytes(12), b"\2" * 32))
        assert struct.unpack("<3H", m.section("META").payload) == (3, 2, 5)

    def test_sound_text(self):
        text = sound.sound_text(ROOT / "generated" / "a.ogg", "sfx")
        assert 'sound: "/generated/a.ogg"' in text and 'group: "sfx"' in text and "looping: 0" in text
