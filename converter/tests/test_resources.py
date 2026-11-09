"""SDL runtime resource containers (converter/kpf/resources.py, docs/formats/packs.md): the page
packer, PACK, MUSC and MSND round trips, grpmeta AFRM and the tileset SHET sheet. The checks of
the generated data run when `generated/` holds it (and compare against the GRPs themselves with
KOPRULU_ORIGINAL)."""
from __future__ import annotations

import os
import random
import struct
import unittest
import zlib
from pathlib import Path

from converter import raster
from converter.emit import pages
from converter.emit.atlas import OpaqueZeroError
from converter.formats import grp, tileset
from converter.formats.palette import TILESETS
from converter.kpf import container, dump, fixtures, kpd, kpm, resources
from converter.tests.test_grpmeta import frame_block, read_grpmeta

ROOT = Path(__file__).resolve().parents[2]
GENERATED = ROOT / "generated"
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
PROFILE = "bw-1.17.0.1"


# --- independent readers of the documented layouts ----------------------------------------

def _label(s: bytes, pos: int) -> tuple[str, int]:
    return s[pos + 1:pos + 1 + s[pos]].decode("latin1"), pos + 1 + s[pos]


def read_sounds(payload: bytes) -> list[tuple[int, bytes]]:
    out, pos = [], 2
    for _ in range(struct.unpack_from("<H", payload)[0]):
        sid, size = struct.unpack_from("<HI", payload, pos)
        out.append((sid, payload[pos + 6:pos + 6 + size]))
        pos += 6 + size
    assert pos == len(payload)
    return out


def read_pack(data: bytes) -> dict:
    c = container.parse(data)
    assert c.kind == resources.PACK_KIND and c.schema_version == resources.PACK_SCHEMA
    a = c.section("ATLS").payload
    atlases, pos = {}, 1
    for _ in range(a[0]):
        name, pos = _label(a, pos)
        atlases[name] = struct.unpack_from("<II", a, pos)
        pos += 8
    assert pos == len(a)
    f = c.section("FRMS").payload
    count = struct.unpack_from("<I", f)[0]
    assert len(f) == 4 + 10 * count
    frames = [struct.unpack_from("<5H", f, 4 + 10 * i) for i in range(count)]
    p = c.section("PAGE").payload
    page_count, width, height = struct.unpack_from("<3H", p)
    page_list, pos = [], 6
    for _ in range(page_count):
        size = struct.unpack_from("<I", p, pos)[0]
        image = raster.decode_png(p[pos + 4:pos + 4 + size])
        assert image.mode == "L" and image.size == (width, height)
        page_list.append(image.tobytes())
        pos += 4 + size
    assert pos == len(p)
    for name in ("FRMS", "PAGE"):
        s = c.section(name)
        assert s.section_class == container.CLASS_DERIVED and s.algorithm_id == pages.ALGORITHM
    return dict(atlases=atlases, frames=frames, pages=page_list, size=width, sounds=read_sounds(c.section("SNDS").payload))


def frame_texels(pack: dict, record: tuple[int, int, int, int, int]) -> bytes:
    page, x, y, w, h = record
    texels, size = pack["pages"][page], pack["size"]
    return b"".join(texels[(y + r) * size + x:(y + r) * size + x + w] for r in range(h))


def read_afrm(data: bytes) -> list[int]:
    payload = container.parse(data).section("AFRM").payload
    count = struct.unpack_from("<H", payload)[0]
    assert len(payload) == 2 + 4 * count
    return list(struct.unpack_from(f"<{count}I", payload, 2))


def read_sheet(payload: bytes) -> tuple[int, int, raster.Image]:
    count, columns = struct.unpack_from("<HH", payload)
    return count, columns, raster.decode_png(payload[4:])


def sheet_tile(image: raster.Image, columns: int, t: int) -> bytes:
    x0, y0 = (t % columns) * tileset.MEGATILE, (t // columns) * tileset.MEGATILE
    data = image.tobytes()
    return b"".join(data[(y0 + r) * image.width + x0:(y0 + r) * image.width + x0 + tileset.MEGATILE]
                    for r in range(tileset.MEGATILE))


# --- synthetic data -----------------------------------------------------------------------

def _frame(rng: random.Random, w: int, h: int) -> grp.Frame:
    mask = bytes(rng.choice((grp.TRANSPARENT, grp.OPAQUE, grp.OPAQUE)) for _ in range(w * h))
    return grp.Frame(rng.randrange(8), rng.randrange(8), w, h,
                     bytes(rng.randrange(1, 256) if m else 0 for m in mask), mask)


def pack_fixture() -> kpd.GrpSet:
    """Two Terran GRPs (one frame repeated, one empty frame) and a Zerg GRP."""
    rng = random.Random(1998)
    a = [_frame(rng, 13, 7), _frame(rng, 30, 2), grp.Frame(0, 0, 0, 0, b"", b"")]
    a.append(a[0])  # identical pixels: stored once
    b = [_frame(rng, rng.randrange(1, 60), rng.randrange(1, 60)) for _ in range(9)]
    z = [_frame(rng, 5, 5)]
    files = {"unit\\terran\\a.grp": grp.encode(64, 64, a), "unit\\terran\\b.grp": grp.encode(64, 64, b),
             "unit\\zerg\\z.grp": grp.encode(16, 16, z)}
    names = ["terran\\a.grp", "zerg\\z.grp", "terran\\b.grp"]
    return kpd.grp_set(len(names), names, {1, 2, 3}, set(), files.__getitem__)


def assert_pack_matches(test: unittest.TestCase, gs: kpd.GrpSet, data: bytes, pack: str) -> None:
    """Every frame of the pack's GRPs, found through grpmeta AFRM and the pack's ATLS, equals
    the GRP frame's indices; texels outside the frames are 0."""
    grpmeta = kpd.build_grpmeta(gs, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes()
    meta, afrm = read_grpmeta(grpmeta), read_afrm(grpmeta)
    p = read_pack(data)
    rest = [bytearray(t) for t in p["pages"]]  # the texels outside every frame
    checked = 0
    for k, (e, g) in enumerate(zip(gs.grps, meta["grps"])):
        name = meta["atlases"][g["atlas"]][0]
        if e.pack != pack:
            test.assertNotIn(name, p["atlases"])
            continue
        first, count = p["atlases"][name]
        for f, frame in enumerate(e.grp.frames):
            index = first + afrm[k] + f
            test.assertLess(afrm[k] + f, count)
            page, x, y, w, h = record = p["frames"][index]
            if not (frame.width and frame.height):
                test.assertEqual(record, (0, 0, 0, 0, 0))
                continue
            test.assertEqual((w, h), (frame.width, frame.height))
            test.assertLessEqual(x + w, p["size"])
            test.assertLessEqual(y + h, p["size"])
            test.assertEqual(frame_texels(p, record), frame.indices)
            for r in range(h):
                rest[page][(y + r) * p["size"] + x:(y + r) * p["size"] + x + w] = bytes(w)
            checked += 1
    for texels in rest:
        test.assertEqual(texels.count(0), len(texels))
    test.assertGreater(checked, 0)


class PagesTest(unittest.TestCase):
    def test_packer_places_without_overlap_inside_pages(self):
        rng = random.Random(7)
        sizes = [(rng.randrange(0, 90), rng.randrange(0, 90)) for _ in range(700)]
        placements, count = pages.pack(sizes, 256)
        assert (placements, count) == pages.pack(list(sizes), 256)  # deterministic
        assert count >= -(-sum(w * h for w, h in sizes) // 256 ** 2)
        grids = [bytearray(256 * 256) for _ in range(count)]
        for (w, h), at in zip(sizes, placements):
            if not (w and h):
                assert at == pages.EMPTY
                continue
            assert 0 <= at.page < count and at.x + w <= 256 and at.y + h <= 256
            grid = grids[at.page]
            for r in range(h):
                row = slice((at.y + r) * 256 + at.x, (at.y + r) * 256 + at.x + w)
                assert not any(grid[row]), "overlap"
                grid[row] = b"\1" * w

    def test_packer_fills_a_page_before_opening_the_next(self):
        placements, count = pages.pack([(64, 64)] * 16 + [(1, 1)], 256)
        assert count == 2 and {p.page for p in placements[:16]} == {0} and placements[16].page == 1
        with self.assertRaises(pages.PageError):
            pages.pack([(257, 1)], 256)

    def test_render_copies_frames_and_clears_the_rest(self):
        images = [(3, 2, bytes([1, 2, 3, 4, 5, 6])), (0, 0, b""), (1, 1, b"\x09")]
        placements, count = pages.pack([(w, h) for w, h, _ in images], 4)
        (page,) = pages.render(images, placements, count, 4)
        image = raster.decode_png(pages.png(page, 4))
        assert image.mode == "L" and image.tobytes() == page
        assert sorted(page) == [0] * 9 + [1, 2, 3, 4, 5, 6, 9]
        at = placements[0]
        assert page[at.y * 4 + at.x:at.y * 4 + at.x + 3] == bytes([1, 2, 3])


class ResourcesTest(unittest.TestCase):
    def test_pack_round_trip(self):
        gs = pack_fixture()
        sounds = [(9, b"OggS-nine"), (2, b"OggS-two"), (5, b"")]
        c, stats = resources.build_pack(gs, "terran", sounds, profile_id=PROFILE, data_set_hash=b"\5" * 32)
        data = c.to_bytes()
        assert data == resources.build_pack(gs, "terran", sounds, profile_id=PROFILE,
                                            data_set_hash=b"\5" * 32)[0].to_bytes()
        assert container.parse(data).source_hash == b"\5" * 32
        p = read_pack(data)
        assert p["atlases"] == {"terran": (0, 4 + 9)}
        assert p["sounds"] == sorted(sounds)
        assert stats.frames == 13 and stats.unique_frames == 11 and stats.pages == 1 and stats.sounds == 3
        assert p["frames"][0] == p["frames"][3]
        assert_pack_matches(self, gs, data, "terran")
        assert_pack_matches(self, gs, resources.build_pack(gs, "zerg", [], profile_id=PROFILE,
                                                           data_set_hash=bytes(32))[0].to_bytes(), "zerg")
        text = dump.dump(data)
        assert "1 atlases: terran (frames 0..12)" in text and "3 sounds" in text and "1 pages of 2048x2048" in text

    def test_pack_rejects_opaque_index_zero(self):
        gs, _ = fixtures.grpmeta_fixture()  # its effects GRP draws an opaque index 0
        resources.build_pack(gs, "terran", [], profile_id=PROFILE, data_set_hash=bytes(32))
        with self.assertRaises(OpaqueZeroError):
            resources.build_pack(gs, "effects", [], profile_id=PROFILE, data_set_hash=bytes(32))

    def test_afrm_is_the_first_frame_within_the_atlas(self):
        gs = pack_fixture()
        data = kpd.build_grpmeta(gs, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes()
        assert [e.atlas for e in gs.grps] == ["terran", "zerg", "terran"]
        assert read_afrm(data) == [0, 0, 4]
        assert "3 GRP entries, first frames in their atlas [0, 0, 4]" in dump.dump(data)

    def test_music_and_mission_sound_round_trips(self):
        tracks = [(False, b""), (True, b"OggS-loop"), (False, b"OggS-once"), (True, b"OggS-loop")]
        c = container.parse(resources.build_music(tracks, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes())
        assert c.kind == resources.MUSIC_KIND and c.schema_version == 1
        payload, pos, back = c.section("TRKS").payload, 1, []
        for _ in range(payload[0]):
            flags, size = struct.unpack_from("<BI", payload, pos)
            back.append((bool(flags & resources.TRACK_LOOPS), payload[pos + 5:pos + 5 + size]))
            pos += 5 + size
        assert back == tracks and pos == len(payload)
        sounds = [(1, b"b"), (0, b"a")]
        m = container.parse(resources.build_mission_sounds(sounds, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes())
        assert m.kind == resources.MISSION_SOUNDS_KIND and read_sounds(m.section("SNDS").payload) == sorted(sounds)
        empty = container.parse(resources.build_mission_sounds([], profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes())
        assert empty.section("SNDS").payload == b"\0\0"

    def test_tileset_sheet(self):
        cv5, vx4, _, vf4 = fixtures.fixture_tileset()
        rng = random.Random(3)
        vr4 = bytes(rng.randrange(256) for _ in range(tileset.VR4_RECORD * 4))
        vx4 = struct.pack(f"<{len(vx4) // 2}H", *(rng.randrange(8) for _ in range(len(vx4) // 2)))
        files = {"cv5": cv5, "vx4": vx4, "vr4": vr4, "vf4": vf4, "wpe": bytes(1024), "grp": fixtures.creep_edge_grp()}
        c = container.parse(kpd.build_tileset("badlands", files, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes())
        assert c.section("EDGE").payload == kpd.creep_edges(files["grp"], "badlands")
        count, columns, image = read_sheet(c.section("SHET").payload)
        ts = tileset.decode(cv5, vx4, vr4, vf4)
        assert (count, columns) == (ts.megatile_count, kpd.SHEET_COLUMNS) == (40, 128)
        assert image.mode == "L" and image.size == (128 * 32, 32)
        for t in range(count):
            assert sheet_tile(image, columns, t) == ts.render_megatile(t)
        assert not any(image.tobytes()[count * 32:image.width])  # the rest of the row is 0

    def test_creep_edges_in_their_cells(self):
        payload = kpd.creep_edges(fixtures.creep_edge_grp(), "badlands")
        assert struct.unpack_from("<H", payload) == (2,)
        image = raster.decode_png(payload[2:])
        assert image.mode == "L" and image.size == (2 * 32, 32)
        data = image.tobytes()
        cell = [data[r * 64 + 32:r * 64 + 64] for r in range(32)]  # frame 1: 2 x 3 at (30, 29), its middle column skipped
        assert cell[29][30:] == bytes([7, 0]) and cell[31][30:] == bytes([9, 0]) and not any(cell[28])
        assert all(data[r * 64:r * 64 + 32] == bytes([5]) * 32 for r in range(32))  # frame 0 fills its tile
        with self.assertRaises(grp.GrpError):  # a frame drawing index 0
            kpd.creep_edges(grp.encode(32, 32, [grp.Frame(0, 0, 1, 1, b"\0", bytes([grp.OPAQUE]))]), "x")


# --- generated data -----------------------------------------------------------------------

PACK_FILES = sorted((GENERATED / "packs").glob("*.kpd")) if (GENERATED / "packs").is_dir() else []
needs_generated = unittest.skipIf(not PACK_FILES or not (GENERATED / "data" / "grpmeta.kpd").exists(),
                                  "generated packs missing (python3 -m converter data / resources)")


def _plain_mask(runs: list[tuple[bool, int]], width: int) -> bytes:
    """The pixels a row's runs cover, clipped to the width (1 opaque, 0 transparent)."""
    out = b"".join((b"\0" if transparent else b"\1") * n for transparent, n in runs)
    return out[:width].ljust(width, b"\0")


NONZERO = bytes([0] + [1] * 255)


@needs_generated
class GeneratedPacksTest(unittest.TestCase):
    def test_pack_pages_hold_every_grp_frame(self):
        """Every GRP frame read back from its pack's pages through grpmeta AFRM: the size of the
        grpmeta frame record, nonzero exactly where the opacity runs are opaque (no stock GRP draws
        an opaque index 0), and with KOPRULU_ORIGINAL equal to the GRP's indices."""
        grpmeta = (GENERATED / "data" / "grpmeta.kpd").read_bytes()
        meta, afrm = read_grpmeta(grpmeta), read_afrm(grpmeta)
        packs = {path.stem: read_pack(path.read_bytes()) for path in PACK_FILES if path.stem != "music"}
        grps = {}
        if ORIGINAL:
            from converter.mpq.profile import Resolver

            with Resolver(ORIGINAL) as r:
                gs = kpd.grp_set(*kpd.grp_references(r.read("arr\\images.dat")[1], r.read("arr\\images.tbl")[1]),
                                 lambda path: r.read(path)[1])
            grps = {e.path: e.grp for e in gs.grps}
        checked = 0
        for k, g in enumerate(meta["grps"]):
            name, pack_name, _ = meta["atlases"][g["atlas"]]
            p = packs[pack_name]
            first, count = p["atlases"][name]
            assert afrm[k] + g["frames"] <= count
            for f in range(g["frames"]):
                _, _, w, h, _ = meta["frames"][g["first"] + f]
                record = p["frames"][first + afrm[k] + f]
                if not (w and h):
                    assert record == (0, 0, 0, 0, 0)
                    continue
                assert record[3:] == (w, h) and record[1] + w <= p["size"] and record[2] + h <= p["size"]
                texels = frame_texels(p, record)
                for r, runs in enumerate(grp.block_rows(frame_block(meta, g["first"] + f), 0, h)):
                    assert texels[r * w:(r + 1) * w].translate(NONZERO) == _plain_mask(runs, w), (g["path"], f, r)
                if grps:
                    assert texels == grps[g["path"]].frames[f].indices, (g["path"], f)
                checked += 1
        assert checked > 0

    def test_tileset_sheets_match_the_viewer_crcs(self):
        for name in TILESETS:
            c = container.parse((GENERATED / "data" / "tilesets" / f"{name}.kpd").read_bytes())
            count, columns, image = read_sheet(c.section("SHET").payload)
            assert columns == kpd.SHEET_COLUMNS and image.width == columns * 32
            assert image.height == -(-count // columns) * 32
            mcrc = GENERATED / "viewer" / f"{name}.mcrc"
            if mcrc.exists():
                crcs = struct.unpack(f"<{count}I", mcrc.read_bytes())
                assert [zlib.crc32(sheet_tile(image, columns, t)) for t in range(count)] == list(crcs), name

    def test_music_and_mission_sounds(self):
        from converter import ui_tables

        music = container.parse((GENERATED / "packs" / "music.kpd").read_bytes())
        assert music.section("TRKS").payload[0] == len(ui_tables.load(ui_tables.PROFILE)["music"])
        kpms = sorted((GENERATED / "missions").glob("*/*.kpm"))
        assert kpms
        for path in kpms:
            wavs = kpm_sound_indices(container.parse(path.read_bytes()).section("WAVS").payload)
            sounds = path.with_name(f"{path.stem}_sounds.kpd")
            c = container.parse(sounds.read_bytes())
            assert c.kind == resources.MISSION_SOUNDS_KIND
            items = read_sounds(c.section("SNDS").payload)
            assert [sid for sid, _ in items] == list(range(len(wavs))) == sorted(wavs), path
            assert all(ogg[:4] == b"OggS" for _, ogg in items)


def kpm_sound_indices(payload: bytes) -> set[int]:
    count = struct.unpack_from("<H", payload)[0]
    indices = {kpm.WAVS_ENTRY.unpack_from(payload, 2 + kpm.WAVS_ENTRY.size * i)[1] for i in range(count)}
    return indices - {kpm.NO_SOUND}
