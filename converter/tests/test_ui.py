"""Dialog BINs: native fixup bytes against the evidence, normalized widgets, ui.kpd."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import unittest
from pathlib import Path

from converter.formats import dialog
from converter.kpf import container as kpf
from converter.kpf import kpd
from converter.kpf.fixtures import dialog_bin as _synthetic, dialog_record

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
PROBE_FILE_BASE = 0x810000  # tools/research/probe_native_dialog_bin_117.py


class UiTest(unittest.TestCase):
    def test_parse_synthetic(self):
        w = dialog.parse(_synthetic())
        assert [x.type for x in w] == [0, 1, 14]
        assert [x.parent for x in w] == [-1, 0, 0]
        assert w[1].text == b"quit"  # flag 0x200: first byte lowercased
        assert w[2].extra == (101, 52, 103, 54)  # control x/y added to the four words
        assert w[2].smks[0].filename == b"port.smk" and (w[2].smks[0].offset_x, w[2].smks[0].offset_y) == (5, 6)

    def test_shared_bitmap_reference(self):
        # Root container (no child) whose bitmap word is negative: a reference relative to the
        # file base, kept raw with flag 0x20; -1 would collide with the ui.kpd "none" value.
        shared = dialog.parse(dialog_record(kind=0, bitmap=0xFFFFFF00))[0]
        assert shared.shared_bitmap == 0xFFFFFF00 and shared.flags & dialog.FLAG_SHARED_BITMAP
        assert dialog.parse(dialog_record(kind=0, bitmap=0x10))[0].shared_bitmap is None
        with self.assertRaises(dialog.DialogError):
            dialog.parse(dialog_record(kind=0, bitmap=dialog.NO_SHARED_BITMAP))

    def test_fixup_synthetic(self):
        tables = tuple(range(100, 115)), tuple(range(200, 215))
        fixed, complete = dialog.fixup(_synthetic(), 0, 0x1000, *tables)
        assert complete
        assert struct.unpack_from("<I", fixed, 0x42)[0] == 0x1000 + 86  # root child relocated
        assert fixed[300] == ord("q")
        assert struct.unpack_from("<II", fixed, 86 + 0x2A) == (101, 201)

    def test_dialogs_section(self):
        dlgs, pool = kpd.dialogs_section({"rez\\x.bin": _synthetic()})
        assert struct.unpack_from("<H", dlgs, 0)[0] == 1
        assert pool == b"quit\0port.smk\0"
        name_len = dlgs[2]
        widgets, smks = struct.unpack_from("<HH", dlgs, 3 + name_len)
        assert (widgets, smks) == (3, 1)
        first = kpd.UI_WIDGET.unpack_from(dlgs, 7 + name_len + kpd.UI_WIDGET.size)
        assert first[0] == 0 and first[2] == 7 and first[10:12] == (0, 4)

    def test_build_parses_back(self):
        widgets = [{"type": 0, "id": 0, "flags": 8, "rect": [70, 8, 500, 384], "text": "Video"},
                   {"type": 1, "id": -2, "flags": 0xA18, "rect": [126, 344, 104, 28], "text": "o\x04O\x01K"},
                   {"type": 6, "id": 1, "flags": 0x18, "rect": [34, 58, 186, 12]}]
        parsed = dialog.parse(dialog.build(widgets))
        assert [(w.type, w.identifier, w.parent) for w in parsed] == [(0, 0, -1), (1, 0xFFFE, 0), (6, 1, 0)]
        # the container's x2/y2 and bitmap size are its size (as the original's BINs have them), a child's its corner
        assert parsed[0].rect == (70, 8, 499, 383) and parsed[0].size == (500, 384)
        assert parsed[0].responsive == (500, 384, 0, 0)
        assert parsed[1].rect == (126, 344, 229, 371) and parsed[1].text == b"o\x04O\x01K"
        assert parsed[2].text is None
        with self.assertRaises(dialog.DialogError):
            dialog.build(widgets[1:])  # no container first

    def test_our_dialogs(self):
        """koprulu\\video.bin: one container, the gamma slider, OK and Cancel, and radio groups whose IDs run on
        (a group is the run of consecutive IDs: SelectOptionInGroup) within the original's 640 x 480 screen."""
        bins = kpd.koprulu_dialogs()
        assert list(bins) == ["koprulu\\video.bin"]
        widgets = dialog.parse(bins["koprulu\\video.bin"])
        root = widgets[0]
        x, y = root.rect[:2]
        assert x + root.size[0] <= 640 and y + root.size[1] <= 480 and root.size[0] <= 502  # the narrow layout's
        ids = sorted(w.identifier for w in widgets if w.type == 3)
        assert ids == [10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52, 60, 61, 62]
        assert {w.identifier for w in widgets if w.type in (1, 2, 6)} == {0xFFFE, 0xFFFD, 1}
        for w in widgets[1:]:
            assert 0 <= w.rect[0] and w.rect[2] < root.size[0] and 0 <= w.rect[1] and w.rect[3] < root.size[1]

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_unported_widget_kinds_stay_out_of_the_campaign_path(self):
        """The combo box (type 13) and the button with its own bitmap (DrawBitmapButton 0x4c3fb0, a type 1 or 2 record
        with an image word) are outside V1: only the multiplayer, custom game and message filter dialogs have the
        first and no dialog the second (kp_ui_draw.c, kp_ui_core.c carry the non-goal)."""
        from converter.mpq.profile import Resolver

        combo_dialogs, bitmap_buttons = set(), []
        with Resolver(ORIGINAL, full_hash=False) as r:
            for name in kpd.ui_defs()["bins"]:
                try:
                    data = r.read(name)[1]
                except Exception:  # a BIN the profile does not have (rez\gluGameMode.bin)
                    continue
                for w in dialog.parse(data):
                    if w.type == 13:
                        combo_dialogs.add(name)
                    if w.type in (1, 2) and w.shared_bitmap is not None:
                        bitmap_buttons.append((name, w.identifier))
        assert combo_dialogs == {"rez\\gluCreat.bin", "rez\\gluCustm.bin", "rez\\msgfltr.bin"}, combo_dialogs
        assert bitmap_buttons == []

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_console_mask_rows_equal_the_pixel_test(self):
        """IsPointOverConsole 0x43e890 answers 0 above ComputeConsoleMaskRows 0x43e9c0's first row and 1 from its
        second, and asks Storm's mask between them: the mask is the console art's index-0 pixels (the one STransCreateE
        of 0x4cb2b0), so every row outside the band is all transparent above it and all opaque from it, and the
        pixel test of kp_ui_input.c answers the same."""
        from converter.formats import pcx
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL, full_hash=False) as r:
            for letter in "tzpn":
                art = pcx.decode(r.read(f"game\\{letter}console.pcx")[1])
                w, h, px = art.width, art.height, art.pixels
                top = 0
                while top < h and not any(px[top * w:(top + 1) * w]):
                    top += 1
                bottom = top
                while bottom < h and 0 in px[bottom * w:(bottom + 1) * w]:
                    bottom += 1
                assert 0 < top < bottom < h, letter
                assert not any(px[:top * w]), letter
                assert all(px[bottom * w:]), letter

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_native_fixups(self):
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
        assert struct.unpack_from("<H", container.section("DLGS").payload, 0)[0] == (len(kpd.ui_defs()["bins"])
                                                                                  + len(kpd.koprulu_dialogs()))
        assert len(container.section("TFNT").payload) == 12 * 48
        gfil = container.section("GFIL").payload
        defs = kpd.ui_defs()  # bitmaps and SMKs, the themes' backgrounds and popups, glue_files
        assert struct.unpack_from("<H", gfil, 0)[0] == 77 + 12 * (1 + len(defs["theme_files"])) + len(defs["glue_files"])
        # (without `has` the credits screens' scripts are left out)
