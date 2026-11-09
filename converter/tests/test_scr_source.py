"""StarCraft: Remastered as a source of profile bw-1.17.0.1's files (converter/casc/source.py).

Synthetic storages check the lookup order, the moved files, the tileset rebuilds, the manifest's absent and
unreadable names and the report. With KOPRULU_SCR every manifest file is classified on the real install, and
the exact and derived files may not drop below the counts measured on 1.23.10.13515.
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path

from converter.casc.source import CV5_GROUP, ScrSource, vx4_from_vx4ex, write_manifest
from converter.inputs import Entry, Manifest, manifest_path
from converter.mpq.profile import Profile
from converter.source import RecordingSource, SourceError, SourceUnreadable
from converter.tests.test_casc import write_storage

SCR = os.environ.get("KOPRULU_SCR")
CACHE_DIR = Path(__file__).resolve().parents[2] / "build" / "scr-test-cache"  # the converted movies, kept between runs
PROFILE = Profile.load()


def manifest(files: dict[str, bytes], absent=(), unreadable=(), extras=None, palettes=None) -> Manifest:
    return Manifest(PROFILE.id, {n.lower().replace("\\", "/"): Entry(hashlib.sha256(d).hexdigest(), len(d), n,
                                                                    (extras or {}).get(n, {}))
                                 for n, d in files.items()},
                    frozenset(n.lower().replace("\\", "/") for n in absent),
                    frozenset(n.lower().replace("\\", "/") for n in unreadable), {"tbl": {}, "bin": {}},
                    palettes or {})


class SyntheticSourceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def source(self, casc: dict[str, bytes], original: dict[str, bytes], **kw) -> ScrSource:
        write_storage(self.root, casc)
        return ScrSource(self.root, PROFILE, manifest(original, **kw))

    def test_sd_copy_before_the_root_copy(self):
        wav = b"RIFF original 8-bit"
        with self.source({"sound/misc/beep.wav": b"RIFF remastered 16-bit", "SD/sound/misc/beep.wav": wav},
                         {"sound\\Misc\\Beep.wav": wav}) as s:
            assert s.read("sound\\Misc\\Beep.wav")[1] == wav
            assert s.status["sound\\Misc\\Beep.wav"] == ("exact", "SD/sound/misc/beep.wav")

    def test_moved_file_and_enus_locale(self):
        grp, txt = b"\x01\x00dialog frames", b"briefing"
        with self.source({"game/consoles/protoss/dlg.grp": grp, "locales/enUS/Assets/rez/EstP01.txt": txt},
                         {"dlgs\\protoss.grp": grp, "rez\\EstP01.txt": txt}) as s:
            assert s.read("dlgs\\protoss.grp")[1] == grp
            assert s.read("rez\\EstP01.txt")[1] == txt
            assert s.status["rez\\EstP01.txt"][0] == "exact"

    def test_grown_tileset_and_vx4_from_vx4ex(self):
        vf4 = bytes(range(64)) * 2
        vx4 = struct.pack("<6H", 1, 2, 3, 0xFFFF, 0, 7)
        vx4ex = struct.pack("<8I", 1, 2, 3, 0xFFFF, 0, 7, 0x12345, 9)
        cv5 = bytes(range(1, CV5_GROUP + 1)) * 2 + bytes(CV5_GROUP)
        cv5_filled = bytes(range(1, CV5_GROUP + 1)) * 2 + b"\x05" * CV5_GROUP + b"appended"
        casc = {"TileSet/badlands.vf4": vf4 + b"appended", "TileSet/badlands.vx4ex": vx4ex,
                "TileSet/desert.cv5": cv5_filled}
        original = {"tileset\\badlands.vf4": vf4, "tileset\\badlands.vx4": vx4, "tileset\\desert.cv5": cv5}
        with mock_filled({"desert": (2, 3)}), self.source(casc, original) as s:
            for name, data in original.items():
                assert s.read(name)[1] == data, name
                assert s.status[name][0] == "derived", name

    def test_vx4ex_with_wide_references_in_the_prefix_is_not_a_vx4(self):
        assert vx4_from_vx4ex(struct.pack("<2I", 1, 0x10000), 4) is None
        assert vx4_from_vx4ex(struct.pack("<2I", 1, 2), 4) == struct.pack("<2H", 1, 2)

    def test_differs_unlisted_absent_unreadable(self):
        with self.source({"rez/stat_txt.tbl": b"edited", "music/new.ogg": b"ogg", "glue/x.smk": b"",
                          "campaign/p10/wav/a.wav": b"intact"},
                         {"rez\\stat_txt.tbl": b"original"}, absent=("glue\\x.smk",),
                         unreadable=("campaign\\p10\\wav\\a.wav",)) as s:
            assert s.read("rez\\stat_txt.tbl")[1] == b"edited"
            assert s.status["rez\\stat_txt.tbl"][0] == "differs"
            assert s.read("music\\new.ogg")[1] == b"ogg" and s.status["music\\new.ogg"][0] == "unlisted"
            assert not s.has("glue\\x.smk")
            with self.assertRaises(SourceError):
                s.read("glue\\x.smk")
            assert s.has("campaign\\p10\\wav\\a.wav")
            with self.assertRaises(SourceUnreadable):
                s.read("campaign\\p10\\wav\\a.wav")
            with self.assertRaises(SourceError):
                s.read("missing\\file.dat")
            assert s.status["missing\\file.dat"] == ("missing", "")
            counts = s.write_report(self.root / "out")
            assert counts == {"differs": 1, "unlisted": 1, "unreadable": 1, "missing": 1}
            report = json.loads((self.root / "out" / "reports" / "scr-import.json").read_text())
            assert report["build"] == "1.23.10.13515" and report["files"]["rez\\stat_txt.tbl"][0] == "differs"

    def test_pcx_rebuilt_from_rgba_with_a_donors_palette(self):
        from converter.casc import rebuild
        from converter.formats import pcx
        from converter.tests.test_scr_rebuild import dds_rgba

        palette = tuple((i, 255 - i, i // 2) for i in range(256))
        donor = rebuild.pcx_file(1, 1, b"\0", palette, bytes(6))
        original = rebuild.pcx_file(2, 1, bytes([7, 0]), palette, bytes([1, 2, 3, 4, 1, 0]))
        rgba = bytes((*palette[7], 255)) + bytes(4)
        files = {"glue\\PalX\\tFont.pcx": donor, "glue\\PalX\\Backgnd.pcx": original}
        extras = {n: {k: v for k, v in rebuild.describe(n, d).items() if not k.startswith("_")} for n, d in files.items()}
        groups = {extras["glue\\PalX\\Backgnd.pcx"]["palette"]: list(files)}
        with self.source({"glue/palx/tfont.pcx": donor, "SD/glue/palx/backgnd.DDS": dds_rgba(2, 1, rgba)}, files,
                         extras=extras, palettes=groups) as s:
            assert s.read("glue\\PalX\\Backgnd.pcx")[1] == original
            assert s.status["glue\\PalX\\Backgnd.pcx"][0] == "derived"
            assert pcx.decode(s.read("glue\\PalX\\Backgnd.pcx")[1]).palette == palette

    def test_manifest_round_trip_through_a_recording(self):
        files = {"arr\\units.dat": b"units", "rez\\stat_txt.tbl": b"strings"}

        class Inner:
            profile = PROFILE

            def read(self, name):
                if name == "bad.wav":
                    raise SourceUnreadable(name)
                return "MPQ", files[name]

            def has(self, name):
                return name in files or name == "bad.wav"

            def data_set_hash(self):
                return bytes(32)

            def close(self):
                pass

        rec = RecordingSource(Inner())
        for name in files:
            rec.read(name)
        assert not rec.has("glue\\none.pcx")
        with self.assertRaises(SourceUnreadable):
            rec.read("bad.wav")
        path = self.root / "inputs.json"
        write_manifest(path, PROFILE.id, rec.files, rec.absent, rec.unreadable)
        with mock_manifest_dir(self.root):
            (self.root / f"inputs-{PROFILE.id}.json").write_text(path.read_text())
            m = Manifest.load(PROFILE.id)
        assert m.files["arr/units.dat"] == Entry(hashlib.sha256(b"units").hexdigest(), 5, "arr\\units.dat", {})
        assert m.absent == {"glue/none.pcx"} and m.unreadable == {"bad.wav"}


def mock_filled(table):
    from unittest import mock

    return mock.patch.dict("converter.casc.source.CV5_FILLED", table, clear=True)


def mock_manifest_dir(path: Path):
    from unittest import mock

    return mock.patch("converter.inputs.MANIFEST_DIR", path)


class ManifestTest(unittest.TestCase):
    def test_committed_manifest_covers_the_profile(self):
        m = Manifest.load(PROFILE.id)
        assert m.profile == PROFILE.id and manifest_path(PROFILE.id).is_file()
        assert len(m.files) > 4000
        assert "arr/units.dat" in m.files and "tileset/badlands.vx4" in m.files


@unittest.skipIf(not SCR, "KOPRULU_SCR not set")
class RemasteredInstallTest(unittest.TestCase):
    # Measured on 1.23.10.13515 (enUS and ruRU installed); a later build or a reconstruction may raise them.
    MIN_EXACT, MIN_DERIVED, MIN_EQUIVALENT = 2823, 225, 6

    def test_every_manifest_file_is_classified(self):
        with ScrSource(SCR, cache=CACHE_DIR) as s:
            for name in (e.name for e in s.manifest.files.values()):
                try:
                    s.read(name)
                except SourceError:
                    pass
            counts: dict[str, int] = {}
            for status, _ in s.status.values():
                counts[status] = counts.get(status, 0) + 1
        assert counts.get("exact", 0) >= self.MIN_EXACT, counts
        assert counts.get("derived", 0) >= self.MIN_DERIVED, counts
        assert counts.get("equivalent", 0) >= self.MIN_EQUIVALENT, counts
        assert "unlisted" not in counts and "missing" not in counts, counts


if __name__ == "__main__":
    unittest.main()
