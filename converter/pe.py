"""Minimal read-only PE32 reader: virtual-address reads and resource lookup.

Only what the converter needs to check profile tables against StarCraft.exe; the
executable is never run.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path


class PeError(ValueError):
    """Malformed or unsupported PE image."""


@dataclass(frozen=True)
class _Section:
    name: str
    rva: int
    virtual_size: int
    raw_offset: int
    raw_size: int


RT_DIRECTORY_INDEX = 2  # data directory index of the resource table


class PeImage:
    def __init__(self, data: bytes):
        self.data = data
        if data[:2] != b"MZ":
            raise PeError("missing MZ header")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe : pe + 4] != b"PE\0\0":
            raise PeError("missing PE signature")
        _machine, count, _ts, _sym, _nsym, opt_size, _chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x10B:
            raise PeError("not a PE32 image")
        self.image_base = struct.unpack_from("<I", data, opt + 28)[0]
        dirs = opt + 96
        self._resource_rva = struct.unpack_from("<I", data, dirs + 8 * RT_DIRECTORY_INDEX)[0]
        self.sections = []
        table = opt + opt_size
        for i in range(count):
            name, vsize, rva, rsize, roff = struct.unpack_from("<8sIIII", data, table + 40 * i)
            self.sections.append(_Section(name.rstrip(b"\0").decode("ascii"), rva, vsize, roff, rsize))

    @classmethod
    def load(cls, path: str | Path) -> "PeImage":
        return cls(Path(path).read_bytes())

    def rva_to_offset(self, rva: int) -> int:
        for s in self.sections:
            if s.rva <= rva < s.rva + max(s.virtual_size, s.raw_size):
                if rva - s.rva >= s.raw_size:
                    raise PeError(f"RVA {rva:#x} lies in the uninitialised part of {s.name}")
                return s.raw_offset + rva - s.rva
        raise PeError(f"RVA {rva:#x} outside every section")

    def read_va(self, va: int, size: int) -> bytes:
        offset = self.rva_to_offset(va - self.image_base)
        return self.data[offset : offset + size]

    def resource(self, type_name: str | int, name_id: int) -> bytes:
        """Return the first language of resource (type, id). String types match by name."""
        root = self.rva_to_offset(self._resource_rva)

        def entries(directory: int):
            named, ids = struct.unpack_from("<HH", self.data, directory + 12)
            for i in range(named + ids):
                key, target = struct.unpack_from("<II", self.data, directory + 16 + 8 * i)
                if key & 0x80000000:
                    n = root + (key & 0x7FFFFFFF)
                    length = struct.unpack_from("<H", self.data, n)[0]
                    key = self.data[n + 2 : n + 2 + 2 * length].decode("utf-16-le")
                yield key, target

        def child(directory: int, wanted: str | int) -> int:
            for key, target in entries(directory):
                if key == wanted:
                    return target
            raise PeError(f"resource {type_name!r}/{name_id} not found")

        type_dir = child(root, type_name)
        id_dir = child(root + (type_dir & 0x7FFFFFFF), name_id)
        _lang, leaf = next(entries(root + (id_dir & 0x7FFFFFFF)))
        rva, size = struct.unpack_from("<II", self.data, root + leaf)
        offset = self.rva_to_offset(rva)
        return self.data[offset : offset + size]
