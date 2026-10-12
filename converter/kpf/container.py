"""Koprulu container (.kpd/.kpm): writer and reference reader.

Layout is specified in docs/formats/container.md; game/data/kpf.lua is the runtime reader.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field

from ..errors import ConverterError

MAGIC = b"KPRF"
CONTAINER_VERSION = 1
HEADER_SIZE = 128
ENTRY_SIZE = 48
ALIGN = 4
PROFILE_ID_SIZE = 16
HASH_SIZE = 32
INPUTS_HASH_SIZE = 16

CLASS_CANONICAL = 0
CLASS_DERIVED = 1

# magic, kind, container version, header size, entry size, section count, schema version,
# converter version, profile id, source hash, provenance hash, table offset, file size, reserved
_HEADER = struct.Struct("<4s4sHHHHII16s32s32sII16s")
# fourcc, class, reserved, flags, offset, size, schema version, crc32, algorithm id,
# algorithm version, inputs hash
_ENTRY = struct.Struct("<4sBBHIIII4sI16s")
assert _HEADER.size == HEADER_SIZE and _ENTRY.size == ENTRY_SIZE


class ContainerError(ConverterError, ValueError):
    """Malformed or unexpected container data."""


def fourcc(value: str | bytes) -> bytes:
    raw = value.encode("ascii") if isinstance(value, str) else bytes(value)
    if len(raw) != 4:
        raise ContainerError(f"fourcc must be 4 bytes: {value!r}")
    return raw


def _fixed(value: bytes, size: int, what: str) -> bytes:
    if len(value) > size:
        raise ContainerError(f"{what} longer than {size} bytes")
    return value.ljust(size, b"\0")


@dataclass
class Section:
    fourcc: bytes
    payload: bytes
    section_class: int = CLASS_CANONICAL
    flags: int = 0
    schema_version: int = 1
    algorithm_id: bytes = b"\0\0\0\0"
    algorithm_version: int = 0
    inputs_hash: bytes = b"\0" * INPUTS_HASH_SIZE
    offset: int = 0
    crc32: int = 0


@dataclass
class Container:
    kind: bytes
    schema_version: int
    converter_version: int
    profile_id: str
    source_hash: bytes = b"\0" * HASH_SIZE
    provenance_hash: bytes = b"\0" * HASH_SIZE
    sections: list[Section] = field(default_factory=list)

    def add(self, name: str | bytes, payload: bytes, *, schema_version: int = 1, flags: int = 0) -> None:
        self._append(Section(fourcc(name), bytes(payload), CLASS_CANONICAL, flags, schema_version))

    def add_derived(
        self,
        name: str | bytes,
        payload: bytes,
        *,
        algorithm_id: str | bytes,
        algorithm_version: int,
        inputs_hash: bytes,
        schema_version: int = 1,
        flags: int = 0,
    ) -> None:
        self._append(
            Section(
                fourcc(name),
                bytes(payload),
                CLASS_DERIVED,
                flags,
                schema_version,
                fourcc(algorithm_id),
                algorithm_version,
                _fixed(inputs_hash[:INPUTS_HASH_SIZE], INPUTS_HASH_SIZE, "inputs hash"),
            )
        )

    def section(self, name: str | bytes) -> Section:
        key = fourcc(name)
        for s in self.sections:
            if s.fourcc == key:
                return s
        raise KeyError(name)

    def _append(self, section: Section) -> None:
        if any(s.fourcc == section.fourcc for s in self.sections):
            raise ContainerError(f"duplicate section {section.fourcc!r}")
        self.sections.append(section)

    def to_bytes(self) -> bytes:
        table_offset = HEADER_SIZE
        cursor = table_offset + ENTRY_SIZE * len(self.sections)
        for s in self.sections:
            cursor += (-cursor) % ALIGN
            s.offset = cursor
            s.crc32 = zlib.crc32(s.payload) & 0xFFFFFFFF
            cursor += len(s.payload)
        file_size = cursor + (-cursor) % ALIGN

        header = _HEADER.pack(
            MAGIC,
            fourcc(self.kind),
            CONTAINER_VERSION,
            HEADER_SIZE,
            ENTRY_SIZE,
            len(self.sections),
            self.schema_version,
            self.converter_version,
            _fixed(self.profile_id.encode("ascii"), PROFILE_ID_SIZE, "profile id"),
            _fixed(self.source_hash, HASH_SIZE, "source hash"),
            _fixed(self.provenance_hash, HASH_SIZE, "provenance hash"),
            table_offset,
            file_size,
            b"\0" * 16,
        )
        out = bytearray(header)
        for s in self.sections:
            out += _ENTRY.pack(
                s.fourcc, s.section_class, 0, s.flags, s.offset, len(s.payload), s.schema_version,
                s.crc32, s.algorithm_id, s.algorithm_version, s.inputs_hash,
            )
        for s in self.sections:
            out += b"\0" * (s.offset - len(out))
            out += s.payload
        out += b"\0" * (file_size - len(out))
        return bytes(out)


def parse(data: bytes, *, verify_crc: bool = True) -> Container:
    if len(data) < HEADER_SIZE:
        raise ContainerError("file shorter than header")
    (magic, kind, version, header_size, entry_size, count, schema, conv, profile, src, prov,
     table_offset, file_size, _reserved) = _HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        raise ContainerError(f"bad magic {magic!r}")
    if version != CONTAINER_VERSION or header_size != HEADER_SIZE or entry_size != ENTRY_SIZE:
        raise ContainerError(f"unsupported container version {version} (header {header_size}, entry {entry_size})")
    if file_size != len(data):
        raise ContainerError(f"file size {len(data)}, header says {file_size}")
    if table_offset + count * ENTRY_SIZE > len(data):
        raise ContainerError("section table out of bounds")
    c = Container(kind, schema, conv, profile.rstrip(b"\0").decode("ascii"), src, prov)
    for i in range(count):
        (cc, cls, _res, flags, offset, size, sver, crc, alg, alg_ver, inputs) = _ENTRY.unpack_from(
            data, table_offset + i * ENTRY_SIZE
        )
        if offset % ALIGN or offset + size > len(data):
            raise ContainerError(f"section {cc!r} out of bounds or misaligned")
        payload = data[offset : offset + size]
        if verify_crc and zlib.crc32(payload) & 0xFFFFFFFF != crc:
            raise ContainerError(f"section {cc!r} crc mismatch")
        s = Section(cc, payload, cls, flags, sver, alg, alg_ver, inputs, offset, crc)
        c._append(s)
    return c
