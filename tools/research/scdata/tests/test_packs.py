"""Stock sound grouping with KOPRULU_ORIGINAL."""
from __future__ import annotations

import os
import struct
import unittest

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


class PacksTest(unittest.TestCase):
    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_stock_sound_packs(self):
        from scdata import gamedata
        from scdata.kpf.kpd import SOUND_PACKS
        from scdata.mpq.profile import Resolver

        with Resolver(ORIGINAL) as r:
            payload = gamedata.sounds(r).section("PACK").payload
        count = struct.unpack_from("<H", payload)[0]
        by_sound = payload[2:]
        assert len(by_sound) == count
        assert {p: by_sound.count(k + 1) for k, p in enumerate(SOUND_PACKS)} == {
            "terran": 378, "protoss": 341, "zerg": 275, "effects": 145}
        ids = [sid for sid, k in enumerate(by_sound) if k]
        assert len(ids) == 1139 and ids[0] == 1 and ids[-1] == 1143
        assert not {86, 104, 105, 1066} & set(ids)  # sfxdata names files the archives lack
