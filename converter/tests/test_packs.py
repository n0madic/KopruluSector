"""Pack resources (converter/emit/packs.py) on synthetic data; stock sound grouping with
KOPRULU_ORIGINAL."""
from __future__ import annotations

import os
import re
import shutil
import struct
import unittest

from converter import raster
from converter.emit import packs, sound, sprite_probe
from converter.emit.defold import ROOT
from converter.kpf import fixtures

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
OUT = ROOT / "build" / "unittest" / "packs"
has_oggenc = unittest.skipIf(shutil.which("oggenc") is None, "oggenc missing")


class PacksOutputTest(unittest.TestCase):
    """Tests that write pack resources under OUT, which is recreated for each test."""

    def setUp(self) -> None:
        shutil.rmtree(OUT, ignore_errors=True)
        OUT.mkdir(parents=True)
        self.out = OUT

    def tearDown(self) -> None:
        shutil.rmtree(OUT, ignore_errors=True)

    def test_pack_atlases_and_mask_fallback(self):
        gs, _ = fixtures.grpmeta_fixture()
        atlases = packs.write_pack_atlases(gs, self.out)
        assert atlases == {
            "terran": {"pack": "terran", "mask": False, "frames": 3, "pixels": 23 * 9 + 200 * 4 + 1, "first": "terran_fix",
                       "animations": 1},
            "effects": {"pack": "effects", "mask": True, "frames": 1, "pixels": 8, "first": "thingy_fx", "animations": 1},
        }
        terran = (self.out / "atlases" / "terran.atlas").read_text()
        assert re.findall(r'image: "([^"]+)"', terran) == [
            f"/build/unittest/packs/atlases/terran/terran_fix/{i:03d}.png" for i in range(3)]
        assert not (self.out / "atlases" / "terran_mask.atlas").exists()
        # ADR 0003 fallback: the effects GRP draws an opaque index 0, so its pack gets an
        # identically ordered mask atlas and the index image keeps the 0.
        mask_text = (self.out / "atlases" / "effects_mask.atlas").read_text()
        assert re.findall(r'id: "([^"]+)"', mask_text) == ["thingy_fx"]
        index = raster.open_png(self.out / "atlases" / "effects" / "thingy_fx" / "000.png")
        mask = raster.open_png(self.out / "atlases" / "effects_mask" / "thingy_fx" / "000.png")
        assert index.mode == mask.mode == "L" and index.size == mask.size == (4, 2)
        assert list(index.tobytes()) == [0, 0, 0, 0, 0, 7, 0, 0]
        assert list(mask.tobytes()) == [255, 255, 0, 0, 0, 255, 0, 0]
        empty = raster.open_png(self.out / "atlases" / "terran" / "terran_fix" / "002.png")
        assert empty.size == (1, 1) and empty.getpixel((0, 0)) == 0

    def test_sprite_probe_matches_atlas_frames(self):
        gs, _ = fixtures.grpmeta_fixture()
        packs.write_pack_atlases(gs, self.out)
        probe = sprite_probe.decode(sprite_probe.encode(gs, ("terran_fix",)))
        assert list(probe) == ["terran_fix"] and len(probe["terran_fix"]) == 3
        for i, (w, h, pixels) in enumerate(probe["terran_fix"]):
            if w and h:
                png = raster.open_png(self.out / "atlases" / "terran" / "terran_fix" / f"{i:03d}.png")
                assert png.size == (w, h) and png.tobytes() == pixels
            else:
                assert pixels == b""
        with self.assertRaises(sprite_probe.ProbeError):
            sprite_probe.encode(gs, ("missing",))

    def test_sound_objects_naming(self):
        ogg = self.out / "x.ogg"
        instances = packs.write_sound_objects([(0, ogg), (127, ogg), (128, ogg)], self.out, "sounds", "sfx", "sfx")
        assert [i for i, _ in instances] == ["sounds0", "sounds1"]
        first = (self.out / "sounds0.go").read_text()
        assert 'id: "sfx0"' in first and 'id: "sfx127"' in first and "sfx128" not in first
        assert 'component: "/build/unittest/packs/sfx128.sound"' in (self.out / "sounds1.go").read_text()
        assert 'sound: "/build/unittest/packs/x.ogg"' in (self.out / "sfx0.sound").read_text()

    @has_oggenc
    def test_ogg_is_deterministic_and_cached(self):
        wav = fixtures.fixture_wav(4410)
        assert sound.ogg_bytes(wav, 7) == sound.ogg_bytes(wav, 7)
        assert sound.ogg_bytes(wav, 7)[:4] == b"OggS"
        cache = sound.OggCache(self.out)
        target = self.out / "a" / "b.ogg"
        cache.convert([(wav, target)])
        first = target.read_bytes()
        cache = sound.OggCache(self.out)
        cache.convert([(wav, target)])
        assert cache.converted == 0 and target.read_bytes() == first
        target.unlink()
        cache.convert([(wav, target)])
        assert cache.converted == 1 and target.read_bytes() == first
        # The stream serial is fixed per output path, so the bytes do not depend on the run.
        assert struct.unpack_from("<I", first, 14)[0] == sound.serial(target)


class PacksTest(unittest.TestCase):
    def test_names(self):
        assert packs.ogg_name("sound\\Terran\\MARINE\\TMaDth00.wav", "sound\\") == "terran_marine_tmadth00.ogg"
        assert packs.ogg_name("T0M21tad.wav", "") == "t0m21tad.ogg"
        assert packs.mission_id("expterran/terran05a") == "expterran_terran05a"

    def test_mission_ogg_names(self):
        d = "campaign\\terran\\terran01\\staredit\\wav\\"
        names = packs.mission_ogg_names("terran/terran01", [d + "a\\x.wav", d + "b\\X.wav", d + "y.wav", d + "Y.WAV"])
        assert names == {d + "a\\x.wav": "staredit_wav_a_x.ogg", d + "b\\X.wav": "staredit_wav_b_x.ogg",
                         d + "y.wav": "y.ogg", d + "Y.WAV": "y.ogg"}

    def test_minimums(self):
        pack = {"atlases": [{"mask": True}], "portrait_atlases": ["p"], "objects": 3, "sounds": 300, "ogg_files": 290}
        mission = {"objects": 1, "sounds": 80, "ogg_files": 79}
        m = packs.minimums({"a": pack, "b": pack}, {"x": mission, "y": {"objects": 1, "sounds": 2, "ogg_files": 2}})
        assert m["sound.max_sound_data"] == 2 * 290 + 79
        assert m["sound.max_component_count"] == 600
        assert m["collection_proxy.max_count"] == 10
        assert m["sprite.max_count"] == 6
        assert m["resource.max_resources"] == 2 * (1 + 3 + 300 + 290) + 3 * 6 + 1 + 1 + 80 + 79

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_stock_pack_sounds(self):
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL, full_hash=False) as r:
            grouped = packs.pack_sounds(r)
        assert {k: len(v) for k, v in grouped.items()} == {"terran": 378, "protoss": 341, "zerg": 275, "effects": 145}
        ids = sorted(i for v in grouped.values() for i, _ in v)
        assert len(ids) == 1139 and ids[0] == 1 and ids[-1] == 1143
        assert not {86, 104, 105, 1066} & set(ids)  # sfxdata names files the archives lack
