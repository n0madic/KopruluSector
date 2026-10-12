"""DAT layouts, dat.kpd and strings.kpd; original-data checks when KOPRULU_ORIGINAL is set."""
from __future__ import annotations

import hashlib
import json
import os
import struct
from pathlib import Path

import pytest

from converter.formats import dat
from converter.kpf import container as kpf
from converter.kpf import kpd

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
LAYOUTS = dat.load_layouts()
UNITS = next(f for f in LAYOUTS if f.table == "units")


def _filled(layout: dat.DatFile) -> bytes:
    return bytes(i % 251 for i in range(layout.size))


def test_layout_sizes_match_research():
    evidence = json.loads((ROOT / "docs/research/evidence/dat-schemas.json").read_text())
    sizes = {f["file"]: f["expected_bytes"] for f in evidence["files"]}
    assert {f.path: f.size for f in LAYOUTS} == sizes
    assert len({f.fourcc for f in LAYOUTS}) == len(LAYOUTS) == 11


def test_subranges():
    data = _filled(UNITS)
    c = UNITS.column("infestation")
    assert dat.value(UNITS, data, "infestation", 106) == struct.unpack_from("<H", data, c.offset)[0]
    with pytest.raises(IndexError):
        dat.value(UNITS, data, "infestation", 105)
    ext = UNITS.column("unit_extents")
    assert dat.value(UNITS, data, "unit_extents", 1, 3) == struct.unpack_from("<H", data, ext.offset + 2 * 7)[0]
    with pytest.raises(IndexError):
        dat.value(UNITS, data, "ready_sound", 106)  # sound columns cover units 0-105
    with pytest.raises(dat.DatError):
        dat.values(UNITS, data[:-1], "graphics")


def test_dat_kpd_round_trip():
    files = {f.path: _filled(f) for f in LAYOUTS}
    c = kpf.parse(kpd.build_dat(files, LAYOUTS, profile_id="p", data_set_hash=bytes(32)).to_bytes())
    assert c.kind == kpd.DAT_KIND
    for f in LAYOUTS:
        assert c.section(f.fourcc).payload == files[f.path]
    cols = c.section(kpd.COLUMNS).payload
    assert struct.unpack_from("<H", cols, 0)[0] == len(LAYOUTS)
    with pytest.raises(dat.DatError):
        kpd.build_dat({**files, UNITS.path: b"x"}, LAYOUTS, profile_id="p", data_set_hash=bytes(32))


@needs_original
def test_layouts_match_executable():
    from converter.pe import PeImage

    pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
    for f in LAYOUTS:
        for i, c in enumerate(f.columns):
            assert struct.unpack("<3I", pe.read_va(f.descriptor_va + 12 * i, 12)) == (
                c.destination_va, c.element_size, c.count), f"{f.path} column {i}"
        assert struct.unpack("<I", pe.read_va(f.descriptor_va + 12 * len(f.columns), 4))[0] == 0


@needs_original
def test_effective_files():
    from converter.mpq.profile import Resolver

    evidence = json.loads((ROOT / "docs/research/evidence/dat-schemas.json").read_text())
    hashes = {f["file"]: f["actual_sha256"] for f in evidence["files"]}
    with Resolver(ORIGINAL, full_hash=False) as r:
        for f in LAYOUTS:
            data = r.read(f.path)[1]
            dat.check(f, data)
            assert hashlib.sha256(data).hexdigest() == hashes[f.path]
        # Spot values from the research: Marine (0) 40 HP stored as 40 * 256; Command Center (106) 1500.
        units = r.read(UNITS.path)[1]
        assert dat.value(UNITS, units, "hit_points", 0) == 40 * 256
        assert dat.value(UNITS, units, "hit_points", 106) == 1500 * 256
        strings = {p: r.read(p)[1] for p in kpd.TBL_FILES.values()}
    c = kpd.build_strings(strings, profile_id="p", data_set_hash=bytes(32))
    stat = c.section("STAT").payload
    assert struct.unpack_from("<H", stat, 0)[0] == 1547  # entry count (native text research)
    assert stat == strings["rez\\stat_txt.tbl"]
