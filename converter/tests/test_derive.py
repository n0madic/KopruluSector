"""Terrain-derived mission sections (WALK, RGNS, CONT) against native 1.17 outputs.

Synthetic cases come from the research evidence (docs/research/evidence/
native-region-generation-probes.json, native-contours-probes.json) and run without original
data. With KOPRULU_ORIGINAL set, all 68 campaign CHKs are converted and hashed exactly as the
research probes hash the native snapshots (tools/research/probe_native_campaign_regions_117.py,
probe_native_contours_117.py).
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import unittest
from pathlib import Path

from converter.derive import contours, regions, walk
from converter.kpf import container as kpf
from converter.kpf import dump, kpm
from converter.tests.private import needs

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/research/evidence"
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _cases(name: str, prefix: str) -> list[dict]:
    data = json.loads((EVIDENCE / name).read_text())
    return [c for c in data["cases"] if c["name"].startswith(prefix)]


def _vf4(records: list[list[int]]) -> bytes:
    return b"".join(struct.pack("<16H", *r) for r in records)


def _snapshot(g: regions.Graph, *, synthetic: bool) -> dict:
    """The probes' snapshot layout: synthetic cases use hex classes and add zero counts."""
    rs = []
    for i in range(g.count):
        r = {"id": i, "flags": hex(g.classes[i]) if synthetic else g.classes[i], "group": g.groups[i],
             "tiles": g.tiles[i], "ground_neighbors": g.ground[i], "neighbors": list(g.neighbors[i]),
             "center_fixed8": list(g.centers[i]), "bounds_pixels": list(g.bounds[i]), "cut_rank": g.cut_ranks[i]}
        if synthetic:
            r["zero_neighbors"] = g.zero_neighbors[i]
        rs.append(r)
    raster = [list(g.raster[y * g.width:(y + 1) * g.width]) for y in range(g.height)]
    splits = [list(s) for s in g.splits]
    if synthetic:
        return {"regions": rs, "split_count": len(splits), "raster": raster, "splits": splits}
    return {"width": g.width, "height": g.height, "regions": rs, "raster": raster, "splits": splits}


def _builder(width: int = 8, height: int = 8) -> regions._Builder:
    return regions._Builder(width, height, [0] * (width * height), bytes(width * height * 16))


def _lcg_records(seed: int, n: int, axes: int, starts: int) -> list[tuple[int, int, int, int, int]]:
    out, x = [], seed
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        axis = (x >> 8) % axes - axes // 2
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((axis, (x >> 8) % starts - 1, i, i & 3, i & 0xFF))
    return out


# Output hashes of the original qsort 0x4d4490 with comparator 0x48ad40, executed under
# Unicorn on these generated arrays (many equal keys; the order differs from a stable sort).
QSORT_NATIVE = [
    (1, 7, 2, 2, "7cdd588c46d0d557977aaacdc94582cc265138bd469c84f3b19434d3852c37a9"),
    (2, 9, 2, 3, "ad7af937116203cf55e19224e1634945f377a49190ef7c5b61777c15ee28569b"),
    (3, 40, 3, 4, "f781e05a3d6ac99a3de1de85e37cee4beac3dbda0db916e360f250180e359bdc"),
    (4, 300, 5, 6, "297a5d0ad0d735d5a06d2a3857b151d86fdd82e2ae4bebd5c8fc1ad554951e39"),
    (5, 2000, 20, 30, "ec00915e114f312ab4cc20e947de765425effd5a9694624eb208152e35c19974"),
    (6, 1000, 1, 1, "371084bbe85f6a5915a116d70d2e0a9bd24b216d54a56e6f16e0656e73f21c97"),
    (7, 513, 4, 2, "98aafe1090e4d8ce58dcec50e3ab6d9471b16f830da459c0d2e05385921cc707"),
]


class DeriveTest(unittest.TestCase):
    @needs(EVIDENCE)
    def test_walk_grid_synthetic(self):
        for case in _cases("native-region-generation-probes.json", "walk_grid_"):
            with self.subTest(case["name"]):
                inp = case["inputs"]
                width, height = (int(v) for v in case["name"].split("_")[-1].split("x"))
                grid = walk.walk_grid(width, height, inp["megatile_ids"], _vf4(inp["vf4_records"]))
                assert _sha(grid) == case["observed"]["sha256"]
                assert sum(v < 128 for v in grid) == case["observed"]["walkable_cells"]

    @needs(EVIDENCE)
    def test_region_builder_synthetic(self):
        for case in _cases("native-region-generation-probes.json", "builder_"):
            with self.subTest(case["name"]):
                inp = case["inputs"]
                width, height = inp["width"], inp["height"]
                vf4 = _vf4(inp["vf4_records"])
                grid = walk.walk_grid(width, height, inp["megatile_ids"], vf4)
                g = regions.build(width, height, inp["tile_flags"], inp["megatile_ids"], vf4, grid)
                assert _snapshot(g, synthetic=True) == case["observed"]
                assert _snapshot(regions.decode(regions.encode(g)), synthetic=True) == case["observed"]

    @needs(EVIDENCE)
    def test_rectangles_synthetic(self):
        cases = _cases("native-region-generation-probes.json", "rectangle_")
        assert len(cases) == 6
        for case in cases:
            size, x, y = case["name"].split("_")[1:]
            width, height = map(int, size.split("x"))
            b = regions._Builder(width, height, [0x10000] * (width * height), bytes(width * height * 16))
            assert b.raster == [regions.CLASS_LOW] * (width * height)
            assert list(b.find_rectangle(int(x), int(y), regions.CLASS_LOW)) == case["observed"], case["name"]

    @needs(EVIDENCE)
    def test_eight_neighbors_synthetic(self):
        for case in _cases("native-region-generation-probes.json", "eight_neighbors_"):
            x, y = map(int, case["name"].split("_")[2:])
            b = _builder()
            b.raster = [1 + i for i in range(64)]
            assert list(b.eight(x, y)) == case["observed"], case["name"]

    @needs(EVIDENCE)
    def test_groups_synthetic(self):
        cases = _cases("native-region-generation-probes.json", "groups_")
        assert len(cases) == 50
        for case in cases:
            inp = case["inputs"]
            b = _builder()
            n = b.count = len(inp["flags"])
            b.cls[:n], b.grp[:n], b.tiles[:n] = inp["flags"], inp["initial_groups"], inp["tile_counts"]
            b.nb[:n], b.ground[:n] = inp["neighbors"], inp["ground_prefix_counts"]
            b.groups()
            assert b.grp[:n] == case["observed"], case["name"]

    @needs(EVIDENCE)
    def test_cut_ranks_synthetic(self):
        cases = _cases("native-region-generation-probes.json", "cut_rank_")
        assert len(cases) == 72
        for case in cases:
            unique, tile_count, ground_skip = map(int, case["name"].split("_")[2:])
            b = _builder()
            b.count = unique + 1
            b.nb[0] = list(range(1, unique + 1))
            b.ground[0] = ground_skip if unique else 0
            for i in range(1, unique + 1):
                b.grp[i], b.tiles[i] = i, tile_count
            b.cut_ranks()
            assert b.rank[0] == case["observed"], case["name"]

    @needs(EVIDENCE)
    def test_contours_synthetic(self):
        for case in _cases("native-contours-probes.json", "synthetic_contours_"):
            with self.subTest(case["name"]):
                inp = case["inputs"]
                width, height = inp["width"], inp["height"]
                grid = walk.walk_grid(width, height, inp["megatile_ids"], _vf4(inp["vf4_records"]))
                c = contours.build(width, height, grid)
                assert c.counts() == case["observed"]["counts"]
                assert _sha(b"".join(contours.encode_records(a) for a in c.arrays)) == \
                    case["observed"]["native_sorted_contours_sha256"]
                assert _sha(contours.trace(width * 4, height * 4, grid)[2]) == case["observed"]["final_walk_sha256"]
                assert contours.decode(contours.encode(c)) == c

    def test_native_qsort_equal_keys(self):
        for seed, n, axes, starts, digest in QSORT_NATIVE:
            with self.subTest(seed=seed, n=n, axes=axes, starts=starts):
                records = _lcg_records(seed, n, axes, starts)
                out = contours.native_qsort(records)
                assert sorted(out) == sorted(records)
                assert _sha(contours.encode_records(out)) == digest

    def test_contour_capacity_growth(self):
        # Mini-cell checkerboard: every walkable cell is its own 4-record contour.
        width, height = 64, 20
        vf4 = _vf4([[(x + y + 1) & 1 for y in range(4) for x in range(4)]])
        grid = walk.walk_grid(width, height, [0] * (width * height), vf4)
        c = contours.build(width, height, grid)
        top = max(c.counts())
        assert top > contours.INITIAL_CAPACITY + contours.CAPACITY_STEP
        cap = c.capacities[0]
        assert c.capacities == [cap] * 4 and (cap - contours.INITIAL_CAPACITY) % contours.CAPACITY_STEP == 0
        assert cap - contours.CAPACITY_STEP < top <= cap  # all four grow together, only when one is full

    def test_mission_sections_round_trip(self):
        from converter.kpf.fixtures import V59, build_chk, chk_strings
        from converter.formats import chk

        width, height = 8, 8
        cv5 = bytearray(52 * 2)
        struct.pack_into("<16H", cv5, 52 + 20, *([1] * 16))  # group 1: megatile 1, blocked
        vf4 = _vf4([[1] * 16, [0] * 16])
        mtxm = struct.pack(f"<{width * height}H", *[0x10 if x == 4 else 0 for y in range(height) for x in range(width)])
        sections = {**V59, "DIM ": struct.pack("<HH", width, height), "MTXM": mtxm, "MASK": bytes(width * height),
                    "STR ": chk_strings(b"name")}
        scenario = chk.load(build_chk(sections), chk.LoaderTables.load(), vcod_sha256=hashlib.sha256(b"vcod").hexdigest())
        data = kpm.build(scenario, [], profile_id="p", data_set_hash=bytes(32), tileset=(bytes(cv5), vf4)).to_bytes()
        c = kpf.parse(data)
        inputs = kpm.terrain_inputs_hash(width, height, mtxm, bytes(cv5), vf4)
        for name, module in (("WALK", walk), ("RGNS", regions), ("CONT", contours)):
            s = c.section(name)
            assert s.section_class == kpf.CLASS_DERIVED and s.algorithm_id == module.ALGORITHM
            assert s.algorithm_version == module.VERSION and s.inputs_hash == inputs
        assert len(c.section("WALK").payload) == width * height * 16
        g = regions.decode(c.section("RGNS").payload)
        assert (g.width, g.height) == (width, height) and g.groups[0] == regions.GROUP_RESERVED
        assert len({g.groups[g.raster[x]] for x in (0, 7)}) == 2  # the wall column separates two groups
        assert sum(contours.decode(c.section("CONT").payload).counts()) > 0
        text = dump.dump(data)
        assert "regions" in text and "contours" in text and "walk" in text

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_native_campaign_outputs(self):
        from converter.catalog import MAPDATA_DAT, MAPDATA_TBL, decode_catalog
        from converter.formats import chk
        from converter.formats.palette import TILESETS
        from converter.mpq.profile import Resolver

        graphs = _cases("native-campaign-regions-probes.json", "campaign_graph_")
        sorted_contours = {c["name"][len("authentic_contours_"):]: c["observed"]
                           for c in _cases("native-contours-probes.json", "authentic_contours_")}
        assert len(graphs) == len(sorted_contours) == 68
        matched = {"walk": 0, "graph": 0, "contours": 0, "catalog": 0, "ingame": 0}
        ingame = json.loads((EVIDENCE / "native-ingame-regions.json").read_text())
        with Resolver(ORIGINAL) as r:
            tables = chk.LoaderTables.load(r.profile.id)
            entries = decode_catalog(r.read(MAPDATA_DAT)[1], r.read(MAPDATA_TBL)[1])
            catalog = {e.chk_path.lower() for e in entries}
            catalog_names = {e.chk_path.lower(): e.name for e in entries}
            files = {n: (r.read(f"tileset\\{n}.cv5")[1], r.read(f"tileset\\{n}.vf4")[1]) for n in TILESETS}
            for case in graphs:
                path = case["name"][len("campaign_graph_"):]
                inp = case["inputs"]
                width, height = inp["width"], inp["height"]
                data = r.read(path)[1]
                if path.lower() in catalog:  # the MTXM the native loader selects, as the converter uses it
                    scenario = chk.load(data, tables, vcod_sha256=tables.vcod_sha256)
                    assert (scenario.u16("DIM "), scenario.u16("DIM ", 1)) == (width, height)
                    assert TILESETS[scenario.u16("ERA ") & 7] == inp["tileset"]
                    mtxm = scenario.sections["MTXM"]
                    matched["catalog"] += 1
                else:  # the four VER=57 maps have no loader descriptor: first MTXM chunk, as the probe
                    mtxm = next(data[c.payload_offset:c.payload_offset + c.size]
                                for c in chk.enumerate_chunks(data) if c.tag == "MTXM")
                c = kpf.Container(kpm.KIND, kpm.SCHEMA, 0, "p")
                kpm.add_terrain_derived(c, width, height, mtxm, *files[inp["tileset"]])
                observed = case["observed"]
                assert _sha(c.section("WALK").payload) == observed["walk_sha256"], path
                # The graph builder against the probe, which runs it on the imported flags alone.
                ids, flags = kpm.import_terrain(width, height, mtxm, *files[inp["tileset"]])
                g = regions.build(width, height, flags, ids, files[inp["tileset"]][1], c.section("WALK").payload)
                snapshot = json.dumps(_snapshot(g, synthetic=False), separators=(",", ":")).encode()
                assert _sha(snapshot) == observed["snapshot_sha256"], path
                assert (g.count, len(g.splits)) == (observed["region_count"], observed["split_count"])
                # The RGNS section against the graph the game builds (after the console strips).
                if path.lower() in catalog:
                    name = catalog_names[path.lower()]
                    game = ingame["maps"][name]
                    section = regions.decode(c.section("RGNS").payload)
                    snapshot = json.dumps(_snapshot(section, synthetic=False), separators=(",", ":")).encode()
                    assert _sha(snapshot) == game["snapshot_sha256"], name
                    matched["ingame"] += 1
                cont = contours.decode(c.section("CONT").payload)
                native = sorted_contours[path]
                assert cont.counts() == native["counts"], path
                assert _sha(b"".join(contours.encode_records(a) for a in cont.arrays)) == \
                    native["native_sorted_contours_sha256"], path
                grown = max(cont.counts()) > contours.INITIAL_CAPACITY
                assert cont.capacities == [contours.INITIAL_CAPACITY + grown * contours.CAPACITY_STEP] * 4
                matched["walk"] += 1
                matched["graph"] += 1
                matched["contours"] += 1
        assert matched == {"walk": 68, "graph": 68, "contours": 68, "catalog": 64, "ingame": 64}
