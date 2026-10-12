"""DAT tables in the layout of the generic loader `0x4442b0`.

Each file is its columns back to back, `count` little-endian elements of `element_size`
bytes each, in descriptor order; the loader rejects a file whose size differs from the
descriptor total (fatal error 24). Layouts live in `defs/dat/<profile>.json` (checked
against StarCraft.exe by converter/tests/test_dat.py).

Values are kept unsigned: whether a field is signed depends on the consuming
instruction (movsx/movzx), which may differ between consumers, so the simulation
applies signedness where it reads a column.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

from ..errors import ConverterError

DEFS_DIR = Path(__file__).resolve().parents[2] / "defs" / "dat"
FORMATS = {1: "B", 2: "H", 4: "I"}


class DatError(ConverterError, ValueError):
    """DAT data does not match the native descriptor."""


@dataclass(frozen=True)
class Column:
    name: str
    element_size: int
    count: int
    first_id: int  # first record id the column covers
    per_id: int  # elements per record id
    destination_va: int
    offset: int  # byte offset inside the file

    @property
    def size(self) -> int:
        return self.element_size * self.count


@dataclass(frozen=True)
class DatFile:
    path: str
    fourcc: str
    descriptor_va: int
    records: int
    columns: tuple[Column, ...]

    @property
    def size(self) -> int:
        return sum(c.size for c in self.columns)

    @property
    def table(self) -> str:
        """Short name used by the runtime, e.g. `units` for `arr\\units.dat`."""
        return self.path.rsplit("\\", 1)[-1].split(".")[0]

    def column(self, name: str) -> Column:
        for c in self.columns:
            if c.name == name:
                return c
        raise KeyError(f"{self.path}: no column {name!r}")


def load_layouts(profile_id: str = "bw-1.17.0.1") -> tuple[DatFile, ...]:
    raw = json.loads((DEFS_DIR / f"{profile_id}.json").read_text())
    files = []
    for f in raw["files"]:
        offset, columns = 0, []
        for name, size, count, first, per, dest in f["columns"]:
            columns.append(Column(name, size, count, first, per, int(dest, 16), offset))
            offset += size * count
        files.append(DatFile(f["path"], f["fourcc"], int(f["descriptor_va"], 16), f["records"], tuple(columns)))
    return tuple(files)


def check(layout: DatFile, data: bytes) -> None:
    if len(data) != layout.size:
        raise DatError(f"{layout.path}: {len(data)} bytes, the native descriptor requires {layout.size}")


def values(layout: DatFile, data: bytes, name: str) -> tuple[int, ...]:
    """All elements of one column, unsigned, in file order."""
    check(layout, data)
    c = layout.column(name)
    return struct.unpack_from(f"<{c.count}{FORMATS[c.element_size]}", data, c.offset)


def value(layout: DatFile, data: bytes, name: str, record: int, element: int = 0) -> int:
    """Element `element` of record id `record`; IndexError outside the column's id range."""
    c = layout.column(name)
    index = (record - c.first_id) * c.per_id + element
    if record < c.first_id or not 0 <= element < c.per_id or index >= c.count:
        raise IndexError(f"{layout.path}:{name} does not cover record {record} element {element}")
    return struct.unpack_from(f"<{FORMATS[c.element_size]}", data, c.offset + index * c.element_size)[0]
