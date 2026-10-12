"""Dialog BINs: native fixup bytes against the evidence, normalized widgets, ui.kpd."""
from __future__ import annotations

import hashlib
import json
import os
import struct
from pathlib import Path

import pytest

from converter.formats import dialog
from converter.kpf import container as kpf
from converter.kpf import kpd
from converter.kpf.fixtures import dialog_bin as _synthetic, dialog_record

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
PROBE_FILE_BASE = 0x810000  # tools/research/probe_native_dialog_bin_117.py


def test_parse_synthetic():
    w = dialog.parse(_synthetic())
    assert [x.type for x in w] == [0, 1, 14]
    assert [x.parent for x in w] == [-1, 0, 0]
    assert w[1].text == b"quit"  # flag 0x200: first byte lowercased
    assert w[2].extra == (101, 52, 103, 54)  # control x/y added to the four words
    assert w[2].smks[0].filename == b"port.smk" and (w[2].smks[0].offset_x, w[2].smks[0].offset_y) == (5, 6)


def test_shared_bitmap_reference():
    # Root container (no child) whose bitmap word is negative: a reference relative to the
    # file base, kept raw with flag 0x20; -1 would collide with the ui.kpd "none" value.
    shared = dialog.parse(dialog_record(kind=0, bitmap=0xFFFFFF00))[0]
    assert shared.shared_bitmap == 0xFFFFFF00 and shared.flags & dialog.FLAG_SHARED_BITMAP
    assert dialog.parse(dialog_record(kind=0, bitmap=0x10))[0].shared_bitmap is None
    with pytest.raises(dialog.DialogError):
        dialog.parse(dialog_record(kind=0, bitmap=dialog.NO_SHARED_BITMAP))


def test_fixup_synthetic():
    tables = tuple(range(100, 115)), tuple(range(200, 215))
    fixed, complete = dialog.fixup(_synthetic(), 0, 0x1000, *tables)
    assert complete
    assert struct.unpack_from("<I", fixed, 0x42)[0] == 0x1000 + 86  # root child relocated
    assert fixed[300] == ord("q")
    assert struct.unpack_from("<II", fixed, 86 + 0x2A) == (101, 201)


def test_dialogs_section():
    dlgs, pool = kpd.dialogs_section({"rez\\x.bin": _synthetic()})
    assert struct.unpack_from("<H", dlgs, 0)[0] == 1
    assert pool == b"quit\0port.smk\0"
    name_len = dlgs[2]
    widgets, smks = struct.unpack_from("<HH", dlgs, 3 + name_len)
    assert (widgets, smks) == (3, 1)
    first = kpd.UI_WIDGET.unpack_from(dlgs, 7 + name_len + kpd.UI_WIDGET.size)
    assert first[0] == 0 and first[2] == 7 and first[10:12] == (0, 4)


@pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
def test_native_fixups():
    from converter.mpq.profile import Resolver
    from converter.pe import PeImage

    pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
    interactions = struct.unpack("<15I", pe.read_va(0x4F10F8, 60))
    draws = struct.unpack("<15I", pe.read_va(0x4F1148, 60))
    evidence = json.loads((ROOT / "docs/research/evidence/native-dialog-bin-probes.json").read_text())
    families = ("authentic_entire_bin_chain_fixup_or_image_boundary", "stock_widget_isolated_fixup_or_image_boundary")
    cases = [c for c in evidence["cases"] if c["case"] in families]
    assert len(cases) == 102
    with Resolver(ORIGINAL, full_hash=False) as r:
        files = {c["inputs"]["resource"]: r.read(c["inputs"]["resource"])[1] for c in cases}
        container = kpf.parse(kpd.build_ui(lambda p: r.read(p)[1], profile_id="p", data_set_hash=bytes(32)).to_bytes())
    for c in cases:
        data = files[c["inputs"]["resource"]]
        first = 0
        if c["case"] == families[1]:
            first, kind = c["inputs"]["offset"], c["inputs"]["type"]
            isolated = bytearray(data)
            struct.pack_into("<I", isolated, first if kind else first + 0x42, 0)
            data = bytes(isolated)
        fixed, complete = dialog.fixup(data, first, PROBE_FILE_BASE, interactions, draws)
        assert hashlib.sha256(fixed).hexdigest() == c["actual"]["fixed_bytes_sha256"], c["inputs"]
        assert complete == c["actual"]["complete"]
    assert struct.unpack_from("<H", container.section("DLGS").payload, 0)[0] == len(kpd.ui_defs()["bins"])
    assert len(container.section("TFNT").payload) == 12 * 48
