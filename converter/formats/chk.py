"""Scenario (CHK) reader that follows the native 1.17 loader tables.

The section set, required flags and payload rules come from `defs/chk/<profile>.json`,
which records the original version descriptors and section tables (verified against
StarCraft.exe by converter/tests/test_chk.py). Chunk enumeration mirrors `0x4c1170`;
dispatch is table order outer, chunk order inner (`0x4c12e0`).

Campaign maps are loaded with the header, briefing and use-map-settings groups. Any
chunk whose tag is neither read by those groups nor in the corpus allowlist of ignored
tags is an error, as is a duplicate of a read tag (the native dispatcher would run its
callback twice; no campaign map has one, so the converter does not model it).
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

from ..errors import ConverterError

DEFS_DIR = Path(__file__).resolve().parents[2] / "defs" / "chk"
CAMPAIGN_GROUPS = ("header", "briefing", "ums")
CHUNK_HEADER = 8
VERSION_TAG = "VER "
ERA_TAG = "ERA "
VCOD_TAG = "VCOD"
ERA_MAX_WITHOUT_EXPANSION = 4


class ChkError(ConverterError, ValueError):
    """The scenario is rejected by the native loader rules or lies outside the supported corpus."""


@dataclass(frozen=True)
class Chunk:
    tag: str
    header_offset: int
    size: int

    @property
    def payload_offset(self) -> int:
        return self.header_offset + CHUNK_HEADER

    def complete(self, data_size: int) -> bool:
        """Native bounded read `0x4c12b0`: payload end may equal, not exceed, the data end."""
        return self.payload_offset + self.size <= data_size


@dataclass(frozen=True)
class Row:
    tag: str
    handler: str
    required: bool


@dataclass(frozen=True)
class Descriptor:
    version: int
    expansion_required: bool
    groups: dict[str, tuple[Row, ...]]

    def rows(self, groups: tuple[str, ...]) -> list[Row]:
        return [row for g in groups for row in self.groups[g]]


@dataclass(frozen=True)
class LoaderTables:
    profile: str
    descriptors: dict[int, Descriptor]
    handlers: dict[str, dict]
    ignored_tags: frozenset[str]
    vcod_resource: tuple[str, int]
    raw: dict

    @classmethod
    def load(cls, profile_id: str = "bw-1.17.0.1") -> "LoaderTables":
        raw = json.loads((DEFS_DIR / f"{profile_id}.json").read_text())
        descriptors = {}
        for d in raw["descriptors"]:
            groups = {name: tuple(Row(t, h, bool(r)) for t, h, r in g["rows"]) for name, g in d["groups"].items()}
            descriptors[d["version"]] = Descriptor(d["version"], d["expansion_required"], groups)
        res = raw["vcod_resource"]
        return cls(raw["profile"], descriptors, raw["handlers"], frozenset(raw["ignored_tags"]),
                   (res["type"], res["id"]), raw)


@dataclass(frozen=True)
class Scenario:
    version: int
    chunks: tuple[Chunk, ...]
    sections: dict[str, bytes]  # every read tag -> payload, in native dispatch order
    ignored: tuple[str, ...]  # allowlisted tags present in the file, in file order

    def u16(self, tag: str, index: int = 0) -> int:
        return struct.unpack_from("<H", self.sections[tag], 2 * index)[0]


def enumerate_chunks(data: bytes) -> list[Chunk]:
    """Chunk list as built by `0x4c1170`.

    The first header is read when 8 bytes are available; each next header is at
    `payload + size`, and enumeration stops when fewer than 8 bytes remain there. The
    payload itself is not bounds-checked here (see `Chunk.complete`). Negative sizes step
    backwards natively; they are rejected because no supported map uses them.
    """
    chunks = []
    header = 0
    while header + CHUNK_HEADER <= len(data):
        tag_raw, size = struct.unpack_from("<4si", data, header)
        tag = tag_raw.decode("latin1")
        if size < 0:
            raise ChkError(f"chunk {tag!r} at {header:#x} has negative size {size} (unsupported)")
        chunks.append(Chunk(tag, header, size))
        header += CHUNK_HEADER + size
    return chunks


def _check_payload(tag: str, rule: dict, payload: bytes, vcod: bytes | None) -> None:
    n = len(payload)
    if "eq" in rule and n != rule["eq"]:
        raise ChkError(f"section {tag!r}: {n} bytes, the native loader requires exactly {rule['eq']}")
    if "max" in rule and n > rule["max"]:
        raise ChkError(f"section {tag!r}: {n} bytes, the native loader accepts at most {rule['max']}")
    if "multiple" in rule and n % rule["multiple"]:
        raise ChkError(f"section {tag!r}: {n} bytes is not a multiple of {rule['multiple']}")
    if rule.get("vcod") and vcod is not None and payload != vcod:
        # Equal bytes give equal hashes; a different block could still hash equal through
        # ComputeVcodHash 0x464560, which the converter does not implement.
        raise ChkError(f"section {tag!r} differs from the executable's VCOD resource")


def load(
    data: bytes,
    tables: LoaderTables,
    *,
    vcod: bytes | None,
    expansion: bool = True,
    groups: tuple[str, ...] = CAMPAIGN_GROUPS,
) -> Scenario:
    """Validate `data` against the native tables and return the sections the game reads.

    `vcod` is the executable's VCOD resource; None skips that comparison (synthetic tests).
    """
    chunks = enumerate_chunks(data)
    by_tag: dict[str, list[Chunk]] = {}
    for c in chunks:
        by_tag.setdefault(c.tag, []).append(c)

    def payload(c: Chunk) -> bytes:
        if not c.complete(len(data)):
            raise ChkError(f"section {c.tag!r} at {c.header_offset:#x}: payload of {c.size} bytes runs past the end")
        return data[c.payload_offset : c.payload_offset + c.size]

    versions = by_tag.get(VERSION_TAG)
    if not versions:
        raise ChkError("missing required section 'VER '")
    raw_version = payload(versions[0])
    if len(raw_version) != 2:
        raise ChkError(f"section 'VER ': {len(raw_version)} bytes, the native loader requires exactly 2")
    version = struct.unpack("<H", raw_version)[0]
    descriptor = tables.descriptors.get(version)
    if descriptor is None:
        raise ChkError(f"CHK version {version} has no native loader descriptor; the original preflight rejects it")
    if descriptor.expansion_required and not expansion:
        raise ChkError(f"CHK version {version} requires Brood War")

    rows = descriptor.rows(groups)
    read_tags = {row.tag for row in rows}
    for c in chunks:
        if c.tag not in read_tags and c.tag not in tables.ignored_tags:
            raise ChkError(f"section {c.tag!r} is neither read by the version {version} loader nor allowlisted")
    for row in rows:
        if row.required and row.tag not in by_tag:
            raise ChkError(f"missing required section {row.tag!r} (version {version})")
    for tag in read_tags:
        if len(by_tag.get(tag, ())) > 1:
            raise ChkError(f"duplicate section {tag!r} (outside the supported corpus)")

    sections: dict[str, bytes] = {}
    for row in rows:
        for c in by_tag.get(row.tag, ()):
            rule = tables.handlers[row.handler]["payload"]
            if rule.get("reads", True):
                body = payload(c)
                _check_payload(row.tag, rule, body, vcod)
                sections[row.tag] = body
    era = struct.unpack("<H", sections[ERA_TAG])[0]
    if era > ERA_MAX_WITHOUT_EXPANSION and not expansion:
        raise ChkError(f"tileset {era} requires Brood War")

    ignored = tuple(c.tag for c in chunks if c.tag not in read_tags)
    return Scenario(version, tuple(chunks), sections, ignored)
