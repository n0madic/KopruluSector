"""Built-in requirement tables of StarCraft.exe (docs/research/native-dat-requirements.md).

Each table is a run of u16 words: a leading zero, then records `id, tokens..., 0xffff`, and a
final 0xffff in place of a record id. The runtime keeps the words unchanged (dat.kpd sections
RQOR, RQRS, RQTU, RQUP) and builds the offset arrays the way the native lazy initializers do.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

from ..errors import ConverterError
from ..pe import PeImage

DEFS = Path(__file__).resolve().parents[2] / "defs" / "dat"
END = 0xFFFF
MAX_WORDS = 0x2000  # far above the largest table (orders, ~1.3 K words)


class RequirementsError(ConverterError, ValueError):
    """A requirement table does not have the native shape."""


@dataclass(frozen=True)
class Table:
    fourcc: str
    domain: str
    base: int
    offsets: str
    capacity: int


def load_tables(profile_id: str = "bw-1.17.0.1") -> tuple[Table, ...]:
    raw = json.loads((DEFS / f"requirements-{profile_id}.json").read_text())
    return tuple(Table(t["fourcc"], t["domain"], int(t["base"], 16), t["offsets"], t["capacity"])
                 for t in raw["tables"])


def words(pe: PeImage, table: Table) -> list[int]:
    """The table's words from the leading zero through the final 0xffff."""
    out = [struct.unpack("<H", pe.read_va(table.base, 2))[0]]
    if out[0] != 0:
        raise RequirementsError(f"{table.fourcc}: no leading zero word at {table.base:#x}")
    at = table.base + 2
    while len(out) < MAX_WORDS:
        record = struct.unpack("<H", pe.read_va(at, 2))[0]
        out.append(record)
        at += 2
        if record == END:
            return out
        while True:
            w = struct.unpack("<H", pe.read_va(at, 2))[0]
            out.append(w)
            at += 2
            if w == END:
                break
    raise RequirementsError(f"{table.fourcc}: no final 0xffff within {MAX_WORDS} words")


def record_ids(table_words: list[int]) -> list[int]:
    """Record ids in table order (section-marker records included)."""
    ids, k = [], 1
    while table_words[k] != END:
        ids.append(table_words[k])
        k += 1
        while table_words[k] != END:
            k += 1
        k += 1
    return ids


def section(table_words: list[int]) -> bytes:
    return struct.pack(f"<{len(table_words)}H", *table_words)
