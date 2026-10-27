"""Shared emitter helpers (no original data needed)."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from converter.emit import atlas
from converter.emit.files import ROOT, cached, link_file, project_path, write_atomic, write_if_changed


class EmitTest(unittest.TestCase):
    def test_animation_id(self):
        assert atlas.animation_id("unit\\Terran\\marine.grp") == "terran_marine"
        assert atlas.animation_id("unit\\thingy\\fire.grp") == "thingy_fire"

    def test_writes_are_whole_and_cached_bytes_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_atomic(root / "a" / "f.bin", b"whole")
            assert (root / "a" / "f.bin").read_bytes() == b"whole"
            assert [p.name for p in (root / "a").iterdir()] == ["f.bin"]  # no temporary file left
            made = []

            def make() -> bytes:
                made.append(1)
                return b"KPV1 data"

            def ok(d: bytes) -> bool:
                return d.startswith(b"KPV1")

            assert cached(root / "c", b"key", ".kpv", make, ok) == b"KPV1 data" and len(made) == 1
            assert cached(root / "c", b"key", ".kpv", make, ok) == b"KPV1 data" and len(made) == 1  # read back
            (path,) = (root / "c").iterdir()
            path.write_bytes(b"KPV")  # a file an interrupted run left: not complete, made again
            assert cached(root / "c", b"key", ".kpv", make, ok) == b"KPV1 data" and len(made) == 2
            assert path.read_bytes() == b"KPV1 data"
            assert cached(None, b"key", ".kpv", make, ok) == b"KPV1 data" and len(made) == 3  # no cache

    def test_link_file_shares_the_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / "hd2" / "p.kpd", Path(tmp) / "hd" / "p.kpd"
            write_if_changed(src, b"clips")
            write_if_changed(dst, b"old")
            link_file(src, dst)
            assert dst.read_bytes() == b"clips" and dst.samefile(src)
            link_file(src, dst)  # already the same file
            write_if_changed(src, b"new clips")  # written whole into a new file: the link keeps the old bytes
            assert dst.read_bytes() == b"clips"

    def test_project_path(self):
        assert project_path(ROOT / "generated" / "x.ogg") == "/generated/x.ogg"
