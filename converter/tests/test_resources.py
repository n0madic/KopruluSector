"""Resource pieces of the SDL runtime (docs/formats/packs.md): the page packer, grpmeta AFRM and the tileset SHET sheet; with KOPRULU_ORIGINAL the stock tilesets' sheets against the terrain CRCs."""
from __future__ import annotations

import os
import random
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from converter import raster
from converter.emit import pages
from converter.formats import grp, tileset
from converter.formats.palette import TILESETS
from converter.kpf import container, dump, fixtures, kpd

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
PROFILE = "bw-1.17.0.1"


# --- independent readers of the documented layouts ----------------------------------------

def _label(s: bytes, pos: int) -> tuple[str, int]:
    return s[pos + 1:pos + 1 + s[pos]].decode("latin1"), pos + 1 + s[pos]


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
    def test_afrm_is_the_first_frame_within_the_atlas(self):
        gs = pack_fixture()
        data = kpd.build_grpmeta(gs, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes()
        assert [e.atlas for e in gs.grps] == ["terran", "zerg", "terran"]
        assert read_afrm(data) == [0, 0, 4]
        assert "3 GRP entries, first frames in their atlas [0, 0, 4]" in dump.dump(data)

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


# --- original data --------------------------------------------------------------------------

@unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
class StockTilesetsTest(unittest.TestCase):
    def test_tileset_sheets_match_the_viewer_crcs(self):
        from converter.emit import terrain
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL) as r, tempfile.TemporaryDirectory() as tmp:
            terrain.generate(r, Path(tmp))
            for name in TILESETS:
                files = {ext: r.read(f"tileset\\{name}.{ext}")[1] for ext in kpd.TILESET_EXTENSIONS}
                c = container.parse(kpd.build_tileset(name, files, profile_id=PROFILE, data_set_hash=bytes(32)).to_bytes())
                count, columns, image = read_sheet(c.section("SHET").payload)
                assert columns == kpd.SHEET_COLUMNS and image.width == columns * 32
                assert image.height == -(-count // columns) * 32
                crcs = struct.unpack(f"<{count}I", (Path(tmp) / "viewer" / f"{name}.mcrc").read_bytes())
                assert [zlib.crc32(sheet_tile(image, columns, t)) for t in range(count)] == list(crcs), name
