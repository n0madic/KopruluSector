"""Portrait SMKs: variant discovery, portraits.kpd, decoded frames against smackw32 hashes."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import unittest
from pathlib import Path

from converter.formats import dat, smk
from converter.kpf import container as kpf
from converter.kpf import kpd

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_data = unittest.skipIf(not ORIGINAL or not smk.available(), "KOPRULU_ORIGINAL or ffmpeg missing")


class PortraitsTest(unittest.TestCase):
    def test_variant_discovery(self):
        layout = next(f for f in dat.load_layouts() if f.table == "portdata")
        data = bytearray(layout.size)
        idle, talk = layout.column("idle_portrait_file"), layout.column("talking_portrait_file")
        struct.pack_into("<I", data, idle.offset, 1)  # row 0 idle -> tbl entry 1
        struct.pack_into("<I", data, talk.offset, 2)
        table = struct.pack("<3H", 2, 6, 14) + b"tx\\Fid0\0" + b"tx\\Tlk0\0"
        files = {"arr\\portdata.dat": bytes(data), "arr\\portdata.tbl": table}
        existing = {"portrait\\tx\\Fid00.smk", "portrait\\tx\\Fid02.smk", "portrait\\tx\\Tlk01.smk"}
        ps = kpd.portrait_set(files.__getitem__, existing.__contains__)
        assert ps.paths == ["portrait\\tx\\Fid00.smk", "portrait\\tx\\Fid02.smk", "portrait\\tx\\Tlk01.smk"]
        assert ps.rows[0] == ((0, 0b101), (2, 0b10))
        assert ps.rows[1] == ((kpd.NONE16, 0), (kpd.NONE16, 0))

    def test_smk_header(self):
        data = b"SMK2" + struct.pack("<IIIiI", 60, 56, 10, -10000, 1) + bytes(84)
        h = smk.header(data)
        assert (h.width, h.height, h.decoded_frames, h.ms_per_frame) == (60, 56, 11, 100.0)
        with self.assertRaises(smk.SmkError):
            smk.header(b"RIFF" + bytes(100))

    @needs_data
    def test_portraits_match_smackw32(self):
        from converter.mpq.profile import Resolver

        evidence = {f["name"].lower(): f for f in
                    json.loads((ROOT / "docs/research/evidence/smk-decoder-equivalence.json").read_text())["files"]}
        with Resolver(ORIGINAL) as r:
            container = kpd.build_portraits(lambda p: r.read(p)[1], r.has, profile_id="p", data_set_hash=bytes(32))
            movies = {p: smk.decode(r.read(p)[1]) for p in kpd.portrait_set(lambda p: r.read(p)[1], r.has).paths}
        assert len(movies) == 519
        checked = 0
        for path, (header, frames) in movies.items():
            row = evidence.get(path.lower())
            assert row is not None, path
            assert len(frames) == row["native_frames"]
            assert hashlib.sha256(smk.rgb24(frames)).hexdigest() == row["native_sha256"], path
            checked += 1
        assert checked == 519
        c = kpf.parse(container.to_bytes())
        assert struct.unpack_from("<H", c.section("ROWS").payload, 0)[0] == 110
