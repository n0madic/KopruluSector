"""grpmeta.kpd: opacity runs for `0x40fd50`, GRP/LO references and atlas assignment.

Synthetic tests always run; with KOPRULU_ORIGINAL the stock GRP and LO files are checked
against the research evidence (opaque index 0 inventory, the blitter corpus packet counts,
native frame bounds and LO offsets).
"""
from __future__ import annotations

import hashlib
import json
import os
import random
import re
import struct
import unittest
from pathlib import Path
from unittest import mock

from converter import packs
from converter.formats import grp, lo
from converter.kpf import container, dump, fixtures, kpd

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs" / "research" / "evidence"
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_data = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")


def read_grpmeta(data: bytes) -> dict:
    """Independent reader of the documented layout (docs/formats/kpd.md)."""
    c = container.parse(data)
    assert c.kind == kpd.GRPMETA_KIND
    t = c.section("TBLX").payload
    count = struct.unpack_from("<H", t)[0]
    refs = [struct.unpack_from("<HH", t, 2 + 4 * i) for i in range(count)]

    def label(s: bytes, pos: int) -> tuple[str, int]:
        return s[pos + 1:pos + 1 + s[pos]].decode("latin1"), pos + 1 + s[pos]

    a = c.section("ATLS").payload
    atlases, pos = [], 1
    for _ in range(a[0]):
        flags = a[pos]
        pack, pos = label(a, pos + 1)
        name, pos = label(a, pos)
        atlases.append((name, pack, flags))
    s = c.section("GRPS").payload
    grps, pos = [], 2
    for _ in range(struct.unpack_from("<H", s)[0]):
        path, pos = label(s, pos)
        frames, width, height, atlas = struct.unpack_from("<3HB", s, pos)
        anim, pos = label(s, pos + 7)
        first = struct.unpack_from("<I", s, pos)[0]
        pos += 4
        grps.append(dict(path=path, frames=frames, width=width, height=height, atlas=atlas, anim=anim, first=first))
    f = c.section("FRMS").payload
    frames = [struct.unpack_from("<4BI", f, 4 + 8 * i) for i in range(struct.unpack_from("<I", f)[0])]
    s = c.section("LOFS").payload
    los, pos = [], 2
    for _ in range(struct.unpack_from("<H", s)[0]):
        path, pos = label(s, pos)
        size = struct.unpack_from("<I", s, pos)[0]
        los.append((path, s[pos + 4:pos + 4 + size]))
        pos += 4 + size
    return dict(refs=refs, atlases=atlases, grps=grps, frames=frames, opac=c.section("OPAC").payload, los=los)


def frame_block(meta: dict, index: int) -> bytes:
    start = meta["frames"][index][4]
    end = meta["frames"][index + 1][4] if index + 1 < len(meta["frames"]) else len(meta["opac"])
    return meta["opac"][start:end]


def assert_runs_equivalent(data: bytes, frame: int, block: bytes) -> None:
    """Every row of the encoded block hits like the raw packets (whole-row form of 0x40fd50)."""
    _, _, w, h, base = grp.frame_record(data, frame)
    for row, runs in enumerate(grp.block_rows(block, 0, h)):
        raw = grp.row_hits(grp.row_packets(data, base, row, w), w)
        assert grp.row_hits(runs, w) == raw, (frame, row)


def _packed_sha(value) -> str:
    return hashlib.sha256(json.dumps(value, separators=(",", ":"), sort_keys=True).encode()).hexdigest()


def _signed(v: int, bits: int) -> int:
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


# --- synthetic -------------------------------------------------------------------------

class GrpmetaTest(unittest.TestCase):
    def test_canonical_runs_preserve_every_hit(self):
        rng = random.Random(40)
        for _ in range(3000):
            width = rng.randrange(1, 300)
            packets, covered = [], 0
            while covered < width:
                transparent = rng.random() < 0.5
                n = rng.choice((0, 1, 2, rng.randrange(1, 64 + 64 * transparent)))
                packets.append((transparent, n))
                covered += n
            runs = grp.canonical_runs(packets)
            decoded = [(bool(b & grp.RUN_TRANSPARENT), b & grp.RUN_MAX) for b in runs]
            assert grp.row_hits(decoded, width) == grp.row_hits(packets, width)
            assert all(n for _, n in decoded[1:])

    def test_row_hits_is_the_native_loop(self):
        # 0x40fd50 by hand: x = 0 takes the first packet; a boundary pixel takes the preceding run.
        packets = [(True, 0), (False, 2), (True, 3), (False, 1)]
        assert grp.row_hits(packets, 6) == bytes([0, 1, 1, 0, 0, 0])
        assert grp.row_hits([(False, 3), (True, 3)], 6) == bytes([1, 1, 1, 1, 0, 0])

    def test_fixture_twins_agree_on_every_pixel(self):
        gs, _ = fixtures.grpmeta_fixture()
        meta = read_grpmeta(kpd.build_grpmeta(gs, profile_id="bw-1.17.0.1", data_set_hash=bytes(32)).to_bytes())
        k = 0
        for e in gs.grps:
            for f in range(len(e.grp.frames)):
                x0, y0, w, h, _ = grp.frame_record(e.data, f)
                assert meta["frames"][k][:4] == (x0, y0, w, h)
                block = frame_block(meta, k)
                assert block == grp.opacity_block(e.data, f)
                for y in range(-1, h + 1):
                    for x in range(-1, w + 1):
                        assert grp.runs_pixel_opaque(block, 0, w, h, x, y) == grp.pixel_opaque(e.data, f, x, y), (f, x, y)
                assert_runs_equivalent(e.data, f, block)
                k += 1

    def test_fixture_references_and_atlases(self):
        gs, files = fixtures.grpmeta_fixture()
        meta = read_grpmeta(kpd.build_grpmeta(gs, profile_id="bw-1.17.0.1", data_set_hash=bytes(32)).to_bytes())
        none = kpd.NONE16
        assert meta["refs"] == [(0, none), (none, 0), (1, none), (0, none), (none, none)]
        assert [g["path"] for g in meta["grps"]] == ["unit\\terran\\fix.grp", "unit\\thingy\\fx.grp"]
        assert [g["anim"] for g in meta["grps"]] == ["terran_fix", "thingy_fx"]
        assert meta["atlases"] == [("terran", "terran", 0), ("effects", "effects", kpd.ATLAS_MASK)]
        assert meta["los"] == [("unit\\terran\\fix.lol", files["unit\\terran\\fix.lol"])]
        assert lo.point(meta["los"][0][1], 0, 1) == (127, -128)

    def test_dump_decodes_every_section(self):
        gs, _ = fixtures.grpmeta_fixture()
        text = dump.dump(kpd.build_grpmeta(gs, profile_id="bw-1.17.0.1", data_set_hash=bytes(32)).to_bytes())
        # Every section has a decoder: no raw hex preview follows any section header.
        assert not re.search(r"^  [0-9a-f]{2} [0-9a-f]{2} ", text, re.M)
        assert "terran (pack terran)" in text and "effects (pack effects, mask atlas)" in text
        assert "GRP files" in text and "frame records" in text and "LO files" in text

    @mock.patch.object(packs, "ATLAS_AREA_BUDGET", 100)
    def test_assign_atlases_splits_over_budget(self):
        grps = [("unit\\zerg\\a.grp", 60), ("unit\\zerg\\b.grp", 50), ("unit\\terran\\c.grp", 90),
                ("unit\\zerg\\c.grp", 40), ("unit\\zerg\\d.grp", 200), ("unit\\zerg\\e.grp", 1)]
        assert packs.assign_atlases(grps) == {
            "unit\\zerg\\a.grp": "zerg_1", "unit\\zerg\\b.grp": "zerg_2", "unit\\terran\\c.grp": "terran",
            "unit\\zerg\\c.grp": "zerg_2", "unit\\zerg\\d.grp": "zerg_3", "unit\\zerg\\e.grp": "zerg_4"}

    def test_lo_check(self):
        good = struct.pack("<II", 1, 2) + struct.pack("<I", 12) + bytes(4)
        assert lo.check(good) == (1, 2)
        with self.assertRaises(lo.LoError):
            lo.check(good[:-1])
        with self.assertRaises(lo.LoError):
            lo.check(struct.pack("<II", 3, 0))

    def test_frame_offset_high_bit_is_masked(self):
        data = bytearray(fixtures.raw_grp(4, 4, [(0, 0, 2, [bytes([0x02, 1, 2])])]))
        struct.pack_into("<I", data, 10, struct.unpack_from("<I", data, 10)[0] | 0x80000000)
        assert grp.decode(bytes(data)).frames[0].indices == bytes([1, 2])
        assert grp.pixel_opaque(bytes(data), 0, 1, 0) == 1


# --- original data ---------------------------------------------------------------------

@needs_data
class GrpmetaStockTest(unittest.TestCase):
    stock: tuple[kpd.GrpSet, dict, bytes]

    @classmethod
    def setUpClass(cls) -> None:
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL, full_hash=False) as r:
            refs = kpd.grp_references(r.read("arr\\images.dat")[1], r.read("arr\\images.tbl")[1])
            gs = kpd.grp_set(*refs, lambda p: r.read(p)[1])
            images = r.read("arr\\images.dat")[1]
        meta = read_grpmeta(kpd.build_grpmeta(gs, profile_id="bw-1.17.0.1", data_set_hash=bytes(32)).to_bytes())
        cls.stock = gs, meta, images

    def test_stock_opaque_zero_inventory(self):
        """grp-opaque-zero.json: 833 images.tbl GRP names (832 files), frame and opaque pixel
        counts per file, no opaque index 0 anywhere, so no pack needs a mask atlas."""
        gs, _, _ = self.stock
        evidence = json.loads((EVIDENCE / "grp-opaque-zero.json").read_text())
        by_path = {e.path.lower(): e for e in gs.grps}
        assert len(gs.grps) == 832 and len(gs.grp_of_tbl) == evidence["grp_files"] == 833
        total = 0
        for row in evidence["files"]:
            e = by_path[row["name"].lower()]
            assert hashlib.sha256(e.data).hexdigest() == row["sha256"]
            assert len(e.grp.frames) == row["frames"]
            assert sum(f.opaque_pixels for f in e.grp.frames) == row["opaque_pixels"]
            assert row["opaque_zero_pixels"] == 0 and not e.opaque_zero
            total += row["opaque_pixels"]
        assert total == evidence["opaque_pixels"]
        assert not any(mask for _, _, mask in gs.atlases)
        assert {pack for _, pack, _ in gs.atlases} == {"terran", "protoss", "zerg", "neutral", "effects", "doodads"}

    def test_stock_blitter_corpus_packet_counts(self):
        """native-pixel-blitters-probes.json: RLE packet classes and nontransparent pixels of the
        23 GRPs in its `assets_sha256`."""
        gs, _, _ = self.stock
        evidence = json.loads((EVIDENCE / "native-pixel-blitters-probes.json").read_text())
        corpus = {k.lower(): v for k, v in evidence["assets_sha256"].items() if k.endswith(".grp")}
        counts = {"transparent": 0, "literal": 0, "repeat": 0}
        pixels = 0
        for e in gs.grps:
            if e.path.lower() not in corpus:
                continue
            assert hashlib.sha256(e.data).hexdigest() == corpus.pop(e.path.lower())
            for f in range(len(e.grp.frames)):
                _, _, w, h, base = grp.frame_record(e.data, f)
                for row in range(h):
                    pos = base + struct.unpack_from("<H", e.data, base + 2 * row)[0]
                    for transparent, n in grp.row_packets(e.data, base, row, w):
                        packet = e.data[pos]
                        kind = "transparent" if transparent else ("repeat" if packet & 0x40 else "literal")
                        counts[kind] += 1
                        pos += 1 + (0 if transparent else (1 if packet & 0x40 else n))
                        pixels += 0 if transparent else n
        assert not corpus
        assert counts == evidence["retail_rle_packet_counts"]
        assert pixels == evidence["retail_nontransparent_pixel_count"]
        assert evidence["retail_opaque_zero_count"] == 0

    def test_stock_opacity_runs_match_raw_packets(self):
        """For every row of every frame of all 832 GRPs the stored runs hit like the raw packets;
        the per-pixel twins agree on every pixel of the first frame of each GRP."""
        gs, meta, _ = self.stock
        k = 0
        for e in gs.grps:
            for f in range(len(e.grp.frames)):
                block = frame_block(meta, k)
                assert_runs_equivalent(e.data, f, block)
                if f == 0:
                    _, _, w, h, _ = grp.frame_record(e.data, f)
                    for y in range(h):
                        for x in range(w):
                            assert grp.runs_pixel_opaque(block, 0, w, h, x, y) == grp.pixel_opaque(e.data, f, x, y)
                k += 1
        assert k == len(meta["frames"]) == sum(g["frames"] for g in meta["grps"])

    def test_stock_native_frame_bounds(self):
        """native-image-geometry-probes.json `all_recorded_grp_frames_native_bounds`: the image
        bounds `0x40ee70` computes from the stored canvas and frame rectangles (5,880 cases)."""
        gs, meta, _ = self.stock
        by_sha = {hashlib.sha256(e.data).hexdigest(): i for i, e in enumerate(gs.grps)}
        cases = [c for c in json.loads((EVIDENCE / "native-image-geometry-probes.json").read_text())["cases"]
                 if c["case"] == "all_recorded_grp_frames_native_bounds"]
        assert len(cases) == 5880
        for c in cases:
            g = meta["grps"][by_sha[c["inputs"]["asset_sha256"]]]
            fx, fy, fw, fh, _ = meta["frames"][g["first"] + c["inputs"]["frame"]]
            gw, gh, mirror = g["width"], g["height"], c["inputs"]["mirror"]
            wx = _signed(200 - 7 + (gw // 2 - fw - fx if mirror else fx - gw // 2), 16)
            wy = _signed(200 + 11 + fy - gh // 2, 16)
            bounds = [wx, wy, _signed(max(wx, 0), 16), _signed(max(wy, 0), 16), _signed(max(-wx, 0), 16),
                      _signed(max(-wy, 0), 16), _signed(min(fw + min(wx, 0), 640 - max(wx, 0)), 16),
                      _signed(min(fh + min(wy, 0), 400 - max(wy, 0)), 16)]
            assert _packed_sha(bounds) == c["actual"], c["inputs"]

    def test_stock_lo_points(self):
        """native-image-geometry-probes.json LO cases: images.dat overlay column -> images.tbl ->
        stored LO file, and the native offsets of every recorded point (8,294 cases)."""
        from converter.formats import dat

        gs, meta, images = self.stock
        layout = next(f for f in dat.load_layouts() if f.table == "images")
        columns = [dat.values(layout, images, c) for c in kpd.LO_COLUMNS]  # kinds 0-4, then shield
        cases = json.loads((EVIDENCE / "native-image-geometry-probes.json").read_text())["cases"]
        checked = 0
        for c in cases:
            i = c["inputs"]
            if c["case"] == "all_recorded_ordinary_lo_points_native_offset":
                kind, frame, mirror = i["kind"], i["frame"], i["mirror"]
            elif c["case"] == "all_recorded_shield_lo_direction_points":
                kind, frame, mirror = 5, i["direction"], 0
            else:
                continue
            lo_index = meta["refs"][columns[kind][i["image_id"]] - 1][1]
            path, data = meta["los"][lo_index]
            assert hashlib.sha256(data).hexdigest() == i["asset_sha256"], path
            x, y = lo.point(data, frame, i["point"])
            assert _packed_sha([-x if mirror else x, y]) == c["actual"], i
            checked += 1
        assert checked == 5030 + 3264
