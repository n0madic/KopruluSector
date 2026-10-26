"""Pack sound conversion and naming (converter/emit/packs.py) on synthetic data; stock sound grouping with
KOPRULU_ORIGINAL."""
from __future__ import annotations

import os
import shutil
import struct
import unittest

from converter.emit import packs, sound
from converter.emit.files import ROOT
from converter.kpf import fixtures

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
OUT = ROOT / "build" / "unittest" / "packs"
has_oggenc = unittest.skipIf(shutil.which("oggenc") is None, "oggenc missing")


class PacksOutputTest(unittest.TestCase):
    """Tests that write sound files under OUT, which is recreated for each test."""

    def setUp(self) -> None:
        shutil.rmtree(OUT, ignore_errors=True)
        OUT.mkdir(parents=True)
        self.out = OUT

    def tearDown(self) -> None:
        shutil.rmtree(OUT, ignore_errors=True)

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

    def test_mission_ogg_names(self):
        d = "campaign\\terran\\terran01\\staredit\\wav\\"
        names = packs.mission_ogg_names("terran/terran01", [d + "a\\x.wav", d + "b\\X.wav", d + "y.wav", d + "Y.WAV"])
        assert names == {d + "a\\x.wav": "staredit_wav_a_x.ogg", d + "b\\X.wav": "staredit_wav_b_x.ogg",
                         d + "y.wav": "y.ogg", d + "Y.WAV": "y.ogg"}

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_stock_pack_sounds(self):
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL, full_hash=False) as r:
            grouped = packs.pack_sounds(r)
        assert {k: len(v) for k, v in grouped.items()} == {"terran": 378, "protoss": 341, "zerg": 275, "effects": 145}
        ids = sorted(i for v in grouped.values() for i, _ in v)
        assert len(ids) == 1139 and ids[0] == 1 and ids[-1] == 1143
        assert not {86, 104, 105, 1066} & set(ids)  # sfxdata names files the archives lack
