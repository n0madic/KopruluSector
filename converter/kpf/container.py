"""Koprulu container (.kpd/.kpm): writer and reference reader.

Layout is specified in docs/formats/container.md; native/kp_core/src/core/kp_kpf.c is the runtime reader.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from typing import BinaryIO

from ..errors import ConverterError

MAGIC = b"KPRF"
CONTAINER_VERSION = 1
HEADER_SIZE = 128
ENTRY_SIZE = 48
ALIGN = 4
FILE_SIZE_MAX = 0xFFFFFFFF  # the header's u32 size and the table's u32 offsets
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
        return b"".join(self.to_parts())

    def to_parts(self) -> list[bytes]:
        """The file's bytes in pieces (the header, the table, each payload and its padding) that join into to_bytes;
        a payload is not copied. ContainerError when the file passes the header's 32-bit size."""
        table_offset = HEADER_SIZE
        cursor = table_offset + ENTRY_SIZE * len(self.sections)
        for s in self.sections:
            cursor += (-cursor) % ALIGN
            s.offset = cursor
            s.crc32 = zlib.crc32(s.payload) & 0xFFFFFFFF
            cursor += len(s.payload)
        file_size = cursor + (-cursor) % ALIGN
        if file_size > FILE_SIZE_MAX:
            raise ContainerError(f"{self.kind} container of {file_size} bytes passes its 32-bit size")

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
        parts = [header]
        for s in self.sections:
            parts.append(_ENTRY.pack(
                s.fourcc, s.section_class, 0, s.flags, s.offset, len(s.payload), s.schema_version,
                s.crc32, s.algorithm_id, s.algorithm_version, s.inputs_hash,
            ))
        at = table_offset + ENTRY_SIZE * len(self.sections)
        for s in self.sections:
            parts += (b"\0" * (s.offset - at), s.payload)
            at = s.offset + len(s.payload)
        parts.append(b"\0" * (file_size - at))
        return parts


def _header(data: bytes, length: int) -> tuple:
    """The header's fields, checked against a file of ``length`` bytes."""
    if len(data) < HEADER_SIZE:
        raise ContainerError("file shorter than header")
    fields = _HEADER.unpack_from(data, 0)
    magic, version, header_size, entry_size, count = fields[0], *fields[2:6]
    table_offset, file_size = fields[11:13]
    if magic != MAGIC:
        raise ContainerError(f"bad magic {magic!r}")
    if version != CONTAINER_VERSION or header_size != HEADER_SIZE or entry_size != ENTRY_SIZE:
        raise ContainerError(f"unsupported container version {version} (header {header_size}, entry {entry_size})")
    if file_size != length:
        raise ContainerError(f"file size {length}, header says {file_size}")
    if table_offset + count * ENTRY_SIZE > length:
        raise ContainerError("section table out of bounds")
    return fields


def _entry(table: bytes, at: int, length: int) -> tuple:
    """A section table entry's fields, its payload checked to lie in a file of ``length`` bytes."""
    fields = _ENTRY.unpack_from(table, at)
    cc, offset, size = fields[0], fields[4], fields[5]
    if offset % ALIGN or offset + size > length:
        raise ContainerError(f"section {cc!r} out of bounds or misaligned")
    return fields


def section_table(f: BinaryIO) -> list[tuple[bytes, int, int]]:
    """A container file's sections as (fourcc, offset, size), from its header and section table alone: the payloads
    are neither read nor checked."""
    length = f.seek(0, 2)
    f.seek(0)
    fields = _header(f.read(HEADER_SIZE), length)
    count, table_offset = fields[5], fields[11]
    f.seek(table_offset)
    table = f.read(count * ENTRY_SIZE)
    return [(e[0], e[4], e[5]) for e in (_entry(table, i * ENTRY_SIZE, length) for i in range(count))]


def parse(data: bytes, *, verify_crc: bool = True) -> Container:
    (_magic, kind, _version, _header_size, _entry_size, count, schema, conv, profile, src, prov,
     table_offset, _file_size, _reserved) = _header(data, len(data))
    c = Container(kind, schema, conv, profile.rstrip(b"\0").decode("ascii"), src, prov)
    for i in range(count):
        (cc, cls, _res, flags, offset, size, sver, crc, alg, alg_ver, inputs) = _entry(
            data, table_offset + i * ENTRY_SIZE, len(data)
        )
        payload = data[offset : offset + size]
        if verify_crc and zlib.crc32(payload) & 0xFFFFFFFF != crc:
            raise ContainerError(f"section {cc!r} crc mismatch")
        s = Section(cc, payload, cls, flags, sver, alg, alg_ver, inputs, offset, crc)
        c._append(s)
    return c
