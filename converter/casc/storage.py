"""Minimal read-only reader of a local CASC installation (StarCraft: Remastered).

The chain from a file name to its bytes:
  .build.info (active row) -> build config Data/config/xx/yy/<build key>;
  build config `root` content key -> encoding table -> encoded key;
  encoded key -> Data/data/*.idx (latest version of each bucket) -> data.NNN archive and offset;
  the archive holds a 30-byte local header, then the BLTE-encoded content.
StarCraft: Remastered's root file is text, one `name|content key` line per file; names use `/`
and mixed case. Lookups here accept either separator and ignore case, so MPQ paths such as
`arr\\units.dat` resolve too. Every read checks the decoded bytes against their content key (MD5).
"""
from __future__ import annotations

import hashlib
import logging
import re
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

from ..errors import ConverterError

log = logging.getLogger(__name__)

BUILD_INFO = ".build.info"
DATA_DIR = "Data"
LOCAL_HEADER_SIZE = 30  # reversed encoded key (16), size (4), flags (2), two checksums (4 + 4)
INDEX_NAME = re.compile(r"^([0-9a-f]{2})([0-9a-f]{8})\.idx$")
ENCODING_HEADER_SIZE = 22
PAGE_INDEX_HASH_SIZE = 16  # MD5 of each page, after its first key
BLTE_MAGIC = b"BLTE"
BLTE_CHUNK_ENTRY_SIZE = 24  # compressed size (4), decompressed size (4), MD5 (16)


class CascError(ConverterError, RuntimeError):
    """The CASC storage is missing, malformed or uses a feature this reader does not decode."""


class CascFileNotFound(CascError):
    """Raised when a name is absent from the storage's root file."""


@dataclass(frozen=True)
class IndexEntry:
    archive: int
    offset: int
    size: int


def normalize(name: str) -> str:
    """Lookup key of a root name or an MPQ path: `/` separators, lower case."""
    return name.replace("\\", "/").lower()


def parse_build_info(text: str) -> dict[str, str]:
    """Return the active row of .build.info as {column name: value}."""
    lines = [line for line in text.splitlines() if line.strip()]
    if len(lines) < 2:
        raise CascError(f"{BUILD_INFO}: no rows")
    columns = [c.split("!", 1)[0] for c in lines[0].split("|")]
    rows = [dict(zip(columns, line.split("|"))) for line in lines[1:]]
    for row in rows:
        if row.get("Active") == "1":
            return row
    return rows[0]


def parse_config(text: str) -> dict[str, list[str]]:
    """Parse a `key = value value ...` configuration file."""
    config = {}
    for line in text.splitlines():
        if " = " in line and not line.startswith("#"):
            key, value = line.split(" = ", 1)
            config[key.strip()] = value.split()
    return config


def parse_index(data: bytes) -> dict[bytes, IndexEntry]:
    """Parse one local index (.idx, version 7): truncated encoded key -> location."""
    if len(data) < 16:
        raise CascError("index: truncated header")
    header_size = struct.unpack_from("<I", data, 0)[0]
    version, _bucket, _unused, size_len, offset_len, key_len, offset_bits = struct.unpack_from("<HBBBBBB", data, 8)
    if version != 7:
        raise CascError(f"index: version {version}, expected 7")
    pos = (8 + header_size + 15) & ~15
    entries_size = struct.unpack_from("<I", data, pos)[0]
    pos += 8
    entry_size = key_len + offset_len + size_len
    if pos + entries_size > len(data):
        raise CascError("index: entries run past the end of the file")
    mask = (1 << offset_bits) - 1
    entries = {}
    for start in range(pos, pos + entries_size - entry_size + 1, entry_size):
        key = data[start:start + key_len]
        location = int.from_bytes(data[start + key_len:start + key_len + offset_len], "big")
        size = int.from_bytes(data[start + key_len + offset_len:start + entry_size], "little")
        entries.setdefault(key, IndexEntry(location >> offset_bits, location & mask, size))
    return entries


def decode_blte(data: bytes) -> bytes:
    """Decode a BLTE stream (plain `N`, zlib `Z` and nested `F` chunks)."""
    if data[:4] != BLTE_MAGIC:
        raise CascError(f"BLTE: bad magic {data[:4]!r}")
    header_size = struct.unpack_from(">I", data, 4)[0]
    if header_size == 0:
        chunks, pos = [len(data) - 8], 8
    else:
        count = int.from_bytes(data[9:12], "big")
        chunks = [struct.unpack_from(">I", data, 12 + i * BLTE_CHUNK_ENTRY_SIZE)[0] for i in range(count)]
        pos = header_size
    out = []
    for size in chunks:
        chunk = data[pos:pos + size]
        pos += size
        mode, body = chunk[:1], chunk[1:]
        if mode == b"N":
            out.append(body)
        elif mode == b"Z":
            out.append(zlib.decompress(body))
        elif mode == b"F":
            out.append(decode_blte(body))
        else:
            raise CascError(f"BLTE: unsupported chunk mode {mode!r}")
    return b"".join(out)


def parse_encoding(data: bytes) -> dict[bytes, bytes]:
    """Parse the encoding table: content key -> first encoded key."""
    if data[:2] != b"EN":
        raise CascError(f"encoding: bad magic {data[:2]!r}")
    ckey_len, ekey_len = data[3], data[4]
    page_size = struct.unpack_from(">H", data, 5)[0] * 1024
    page_count = struct.unpack_from(">I", data, 9)[0]
    espec_size = struct.unpack_from(">I", data, 18)[0]
    pos = ENCODING_HEADER_SIZE + espec_size + page_count * (ckey_len + PAGE_INDEX_HASH_SIZE)
    table = {}
    for p in range(page_count):
        page = data[pos + p * page_size:pos + (p + 1) * page_size]
        q = 0
        while q + 6 + ckey_len <= len(page) and page[q]:
            key_count = page[q]
            ckey = page[q + 6:q + 6 + ckey_len]
            table[ckey] = page[q + 6 + ckey_len:q + 6 + ckey_len + ekey_len]
            q += 6 + ckey_len + ekey_len * key_count
    return table


def parse_root(data: bytes) -> dict[str, list[tuple[str, bytes]]]:
    """Parse StarCraft: Remastered's text root: normalized name -> [(name, content key)], several when names
    differ only in case (the root is case-sensitive)."""
    files: dict[str, list[tuple[str, bytes]]] = {}
    for line in data.decode("utf-8").splitlines():
        if not line:
            continue
        name, sep, ckey = line.rpartition("|")
        if not sep:
            raise CascError(f"root: malformed line {line!r}")
        try:
            key = bytes.fromhex(ckey)
        except ValueError as exc:
            raise CascError(f"root: malformed content key in {line!r}") from exc
        files.setdefault(normalize(name), []).append((name, key))
    return files


def _field(mapping: dict, key: str, where: str):
    """mapping[key], or a CascError naming what lacks it."""
    try:
        return mapping[key]
    except KeyError:
        raise CascError(f"{where}: no {key!r}") from None


class CascStorage:
    """A local CASC installation opened read-only; `root` holds .build.info and Data/."""

    def __init__(self, root: str | Path):
        self.root = Path(root)
        info_path = self.root / BUILD_INFO
        if not info_path.is_file():
            raise CascError(f"no {BUILD_INFO} in {self.root}")
        self.build_info = parse_build_info(info_path.read_text())
        self.version = self.build_info.get("Version", "")
        build_key = _field(self.build_info, "Build Key", BUILD_INFO)
        config_path = self.root / DATA_DIR / "config" / build_key[:2] / build_key[2:4] / build_key
        if not config_path.is_file():
            raise CascError(f"no build configuration {config_path}")
        self.config = parse_config(config_path.read_text())
        self._index = self._load_indices()
        self._archives: dict[int, BinaryIO] = {}
        encoding = _field(self.config, "encoding", config_path.name)
        root = _field(self.config, "root", config_path.name)
        if len(encoding) < 2 or not root:
            raise CascError(f"{config_path.name}: malformed encoding or root keys")
        try:
            encoding_ckey, encoding_ekey, root_ckey = (bytes.fromhex(k) for k in (*encoding[:2], root[0]))
        except ValueError as exc:
            raise CascError(f"{config_path.name}: malformed encoding or root keys") from exc
        self._encoding = parse_encoding(self._read_checked(encoding_ekey, encoding_ckey))
        self._files = parse_root(self._read_content(root_ckey))
        log.debug("CASC %s: %d files", self.version, len(self._files))

    def _load_indices(self) -> dict[bytes, IndexEntry]:
        latest: dict[str, tuple[str, Path]] = {}
        for path in (self.root / DATA_DIR / "data").glob("*.idx"):
            match = INDEX_NAME.match(path.name)
            if match and match.group(2) > latest.get(match.group(1), ("", path))[0]:
                latest[match.group(1)] = (match.group(2), path)
        if not latest:
            raise CascError(f"no local indices in {self.root / DATA_DIR / 'data'}")
        index: dict[bytes, IndexEntry] = {}
        for _, path in sorted(latest.values()):
            for key, entry in parse_index(path.read_bytes()).items():
                index.setdefault(key, entry)
        return index

    def _read_encoded(self, ekey: bytes) -> bytes:
        entry = self._index.get(ekey[:9])
        if entry is None:
            raise CascError(f"encoded key {ekey.hex()} is not in the local storage")
        archive = self._archives.get(entry.archive)
        if archive is None:
            path = self.root / DATA_DIR / "data" / f"data.{entry.archive:03d}"
            if not path.is_file():
                raise CascError(f"no archive {path}")
            archive = self._archives[entry.archive] = path.open("rb")
        archive.seek(entry.offset)
        raw = archive.read(entry.size)
        if len(raw) != entry.size:
            raise CascError(f"data.{entry.archive:03d}: short read at {entry.offset}")
        return decode_blte(raw[LOCAL_HEADER_SIZE:])

    def _read_checked(self, ekey: bytes, ckey: bytes) -> bytes:
        data = self._read_encoded(ekey)
        if hashlib.md5(data).digest() != ckey:
            raise CascError(f"content key {ckey.hex()}: decoded bytes do not match")
        return data

    def _read_content(self, ckey: bytes) -> bytes:
        ekey = self._encoding.get(ckey)
        if ekey is None:
            raise CascError(f"content key {ckey.hex()} is not in the encoding table")
        return self._read_checked(ekey, ckey)

    def names(self) -> list[str]:
        """Every name of the root file, as spelled there."""
        return [name for entries in self._files.values() for name, _ in entries]

    def has(self, name: str) -> bool:
        return normalize(name) in self._files

    def read(self, name: str) -> bytes:
        """Bytes of `name` (`/` or `\\` separators, any case; the exact case among names differing only in it)."""
        entries = self._files.get(normalize(name))
        if entries is None:
            raise CascFileNotFound(f"{name} not found in CASC {self.version}")
        if len(entries) > 1:
            exact = [e for e in entries if e[0] == name.replace("\\", "/")]
            if len(exact) != 1:
                raise CascError(f"{name}: ambiguous in CASC {self.version}: {', '.join(n for n, _ in entries)}")
            entries = exact
        return self._read_content(entries[0][1])

    def close(self) -> None:
        for archive in self._archives.values():
            archive.close()
        self._archives.clear()

    def __enter__(self) -> "CascStorage":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
