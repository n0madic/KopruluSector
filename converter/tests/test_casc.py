"""CASC reader over a synthetic storage, and StarCraft: Remastered against profile bw-1.17.0.1.

The comparison needs KOPRULU_ORIGINAL (StarCraft.exe and the profile MPQs) and KOPRULU_SCR (a
StarCraft: Remastered install directory holding .build.info and Data/); otherwise it is skipped.
"""
from __future__ import annotations

import hashlib
import os
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from unittest import mock

from converter.__main__ import main
from converter.casc.compare import SCRIPTS, Comparison, compare, rules_files
from converter.casc.storage import CascError, CascFileNotFound, CascStorage, decode_blte
from converter.formats.dat import load_layouts

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
SCR = os.environ.get("KOPRULU_SCR")

BUILD_KEY = "0123456789abcdef0123456789abcdef"
OFFSET_BITS = 30
PAGE_KB = 4


def blte(data: bytes, chunk: int = 0, mode: bytes = b"N") -> bytes:
    """BLTE stream of `data`: one headerless plain chunk, or `chunk`-byte chunks in `mode`."""
    if not chunk:
        return b"BLTE" + struct.pack(">I", 0) + b"N" + data
    bodies = []
    for i in range(0, len(data), chunk):
        part = data[i:i + chunk]
        bodies.append((mode + (zlib.compress(part) if mode == b"Z" else part), len(part)))
    header_size = 12 + 24 * len(bodies)
    table = b"".join(struct.pack(">II", len(b), u) + hashlib.md5(b).digest() for b, u in bodies)
    return (b"BLTE" + struct.pack(">I", header_size) + b"\x0f" + len(bodies).to_bytes(3, "big") + table
            + b"".join(b for b, _ in bodies))


def index_file(entries: list[tuple[bytes, int, int, int]]) -> bytes:
    """Version-7 local index of (encoded key, archive, offset, size) entries."""
    header = struct.pack("<HBBBBBBQ", 7, 0, 0, 4, 5, 9, OFFSET_BITS, 1 << 30)
    body = b"".join(key[:9] + ((archive << OFFSET_BITS) | offset).to_bytes(5, "big") + struct.pack("<I", size)
                    for key, archive, offset, size in entries)
    out = struct.pack("<II", len(header), 0) + header
    out += b"\0" * (((len(out) + 15) & ~15) - len(out))
    return out + struct.pack("<II", len(body), 0) + body


def encoding_file(pairs: dict[bytes, tuple[bytes, int]]) -> bytes:
    """Encoding table with one page: content key -> (encoded key, size)."""
    page = b"".join(b"\x01" + size.to_bytes(5, "big") + ckey + ekey for ckey, (ekey, size) in sorted(pairs.items()))
    page += b"\0" * (PAGE_KB * 1024 - len(page))
    header = b"EN\x01\x10\x10" + struct.pack(">HHIIBI", PAGE_KB, PAGE_KB, 1, 0, 0, 0)
    return header + min(pairs) + hashlib.md5(page).digest() + page


def write_storage(root: Path, files: dict[str, bytes], *, encode=blte, extra_index: bytes | None = None) -> None:
    """Write a CASC install holding `files` under `root` (each blob stored with `encode`)."""
    blobs = {}  # content key -> (encoded key, blob, size)

    def add(content: bytes, blob: bytes) -> bytes:
        ckey = hashlib.md5(content).digest()
        blobs[ckey] = (hashlib.md5(blob).digest(), blob, len(content))
        return ckey

    for content in files.values():
        add(content, encode(content))
    root_text = "".join(f"{name}|{hashlib.md5(content).hexdigest()}\n" for name, content in files.items()).encode()
    root_ckey = add(root_text, blte(root_text))
    encoding = encoding_file({ckey: (ekey, size) for ckey, (ekey, _, size) in blobs.items()})
    encoding_ckey, encoding_blob = hashlib.md5(encoding).digest(), blte(encoding, chunk=1000, mode=b"Z")
    encoding_ekey = hashlib.md5(encoding_blob).digest()
    archive, entries = b"", []
    for ekey, blob in [(ekey, blob) for ekey, blob, _ in blobs.values()] + [(encoding_ekey, encoding_blob)]:
        record = b"\0" * 30 + blob
        entries.append((ekey, 0, len(archive), len(record)))
        archive += record
    data = root / "Data" / "data"
    data.mkdir(parents=True)
    (data / "data.000").write_bytes(archive)
    (data / "0000000002.idx").write_bytes(index_file(entries))
    if extra_index is not None:
        (data / "0000000001.idx").write_bytes(extra_index)
    config = root / "Data" / "config" / BUILD_KEY[:2] / BUILD_KEY[2:4]
    config.mkdir(parents=True)
    (config / BUILD_KEY).write_text(f"# Build Configuration\nroot = {root_ckey.hex()}\n"
                                    f"encoding = {encoding_ckey.hex()} {encoding_ekey.hex()}\n")
    (root / ".build.info").write_text(
        "Branch!STRING:0|Active!DEC:1|Build Key!HEX:16|Version!STRING:0\n"
        f"us|0|{'f' * 32}|0.0.0\n"
        f"eu|1|{BUILD_KEY}|1.23.10.13515\n")


class SyntheticStorageTest(unittest.TestCase):
    FILES = {
        "arr/units.dat": bytes(range(256)) * 8,
        "anim/main_000.anim": b"ANIM" + b"\x04\x02" + b"\0" * 64,
        "locales/enUS/Assets/rez/stat_txt.tbl": b"\x01\x00\x04\x00hello\0",
    }

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_reads_every_file_by_either_separator_and_any_case(self):
        write_storage(self.root, self.FILES)
        with CascStorage(self.root) as storage:
            assert storage.version == "1.23.10.13515"  # the active row, not the first
            assert sorted(storage.names()) == sorted(self.FILES)
            for name, content in self.FILES.items():
                assert storage.read(name) == content
            assert storage.read("ARR\\Units.DAT") == self.FILES["arr/units.dat"]
            assert storage.has("arr\\units.dat") and not storage.has("arr/weapons.dat")

    def test_decodes_zlib_chunks(self):
        write_storage(self.root, self.FILES, encode=lambda data: blte(data, chunk=100, mode=b"Z"))
        with CascStorage(self.root) as storage:
            assert storage.read("arr/units.dat") == self.FILES["arr/units.dat"]

    def test_latest_index_of_a_bucket_wins(self):
        units_ekey = hashlib.md5(blte(self.FILES["arr/units.dat"])).digest()
        stale = index_file([(units_ekey, 0, 0, 40)])  # an older version of the bucket points elsewhere
        write_storage(self.root, self.FILES, extra_index=stale)
        with CascStorage(self.root) as storage:
            assert storage.read("arr/units.dat") == self.FILES["arr/units.dat"]

    def test_missing_name(self):
        write_storage(self.root, self.FILES)
        with CascStorage(self.root) as storage, self.assertRaises(CascFileNotFound):
            storage.read("arr/weapons.dat")

    def test_corrupt_content_is_rejected(self):
        write_storage(self.root, self.FILES)
        archive = self.root / "Data" / "data" / "data.000"
        data = bytearray(archive.read_bytes())
        data[data.index(self.FILES["arr/units.dat"][:64]) + 1] ^= 0xFF
        archive.write_bytes(bytes(data))
        with CascStorage(self.root) as storage, self.assertRaises(CascError):
            storage.read("arr/units.dat")

    def test_encrypted_chunk_is_rejected(self):
        with self.assertRaises(CascError):
            decode_blte(blte(b"secret", chunk=6, mode=b"E"))

    def test_missing_install(self):
        with self.assertRaises(CascError):
            CascStorage(self.root)

    def test_partial_install_is_a_casc_error(self):
        write_storage(self.root, self.FILES)
        info = self.root / ".build.info"
        info.write_text(info.read_text().replace("Build Key!HEX:16", "Key!HEX:16"))
        with self.assertRaisesRegex(CascError, "Build Key"):
            CascStorage(self.root)
        write_storage(self.root / "b", self.FILES)
        config = self.root / "b" / "Data" / "config" / BUILD_KEY[:2] / BUILD_KEY[2:4] / BUILD_KEY
        config.write_text("\n".join(line for line in config.read_text().splitlines() if not line.startswith("root")))
        with self.assertRaisesRegex(CascError, "root"):
            CascStorage(self.root / "b")
        config.unlink()
        with self.assertRaisesRegex(CascError, "build configuration"):
            CascStorage(self.root / "b")

    def test_names_differing_in_case_only(self):
        files = {"TileSet/Desert.dds.grp": b"upper", "TileSet/desert.dds.grp": b"lower", "arr/units.dat": b"u"}
        write_storage(self.root, files)
        with CascStorage(self.root) as storage:
            assert sorted(storage.names()) == sorted(files)
            assert storage.read("TileSet\\Desert.dds.grp") == b"upper"
            assert storage.read("TileSet/desert.dds.grp") == b"lower"
            assert storage.read("ARR/UNITS.DAT") == b"u"
            with self.assertRaisesRegex(CascError, "ambiguous"):
                storage.read("tileset/DESERT.dds.grp")


class CommandLineTest(unittest.TestCase):
    """scr-compare: its output and exit status."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        write_storage(self.root / "scr", SyntheticStorageTest.FILES)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def compare_with(self, equal: bool) -> int:
        result = [Comparison("arr\\units.dat", "StarDat.mpq", 2048, 2048, equal)]
        with mock.patch("converter.__main__.Resolver") as resolver, \
                mock.patch("converter.__main__.Profile.load") as load, \
                mock.patch("converter.casc.compare.compare", return_value=result) as compared, \
                self.assertLogs("converter", "INFO"):
            status = main(["--profile", "p", "scr-compare", "--data", "d", "--scr", str(self.root / "scr")])
        load.assert_called_once_with("p")
        assert resolver.call_args.args[0] == "d" and compared.call_count == 1
        return status

    def test_scr_compare_exit_status(self):
        assert self.compare_with(True) == 0
        assert self.compare_with(False) == 1


@unittest.skipIf(not (ORIGINAL and SCR), "KOPRULU_ORIGINAL and KOPRULU_SCR not set")
class RemasteredAgainstProfileTest(unittest.TestCase):
    """Remastered's rules files equal profile bw-1.17.0.1's effective copies byte for byte."""

    @classmethod
    def setUpClass(cls) -> None:
        from converter.mpq.profile import Resolver

        cls.resolver = Resolver(ORIGINAL)
        cls.storage = CascStorage(SCR)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.storage.close()
        cls.resolver.close()

    def check(self, names: tuple[str, ...]) -> None:
        for c in compare(self.resolver, self.storage, names):
            with self.subTest(name=c.name):
                assert c.equal, f"{c.name}: {c.provider} {c.profile_size} bytes, CASC {c.casc_size} bytes"

    def test_dat_tables_equal_the_profile(self):
        names = tuple(layout.path for layout in load_layouts())
        assert len(names) == 11
        self.check(names)

    def test_scripts_equal_the_profile(self):
        self.check(SCRIPTS)


class RulesFilesTest(unittest.TestCase):
    def test_rules_files_are_the_dat_tables_and_scripts(self):
        names = rules_files()
        assert len(names) == 14 and names[-3:] == SCRIPTS
