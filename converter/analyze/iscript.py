"""IScript decoding and reachable closure (plan §5.2, resource closure).

Operands follow the interpreter table of atlas P04 (defs/iscript/<profile>.json). The
closure starts at every animation entry of a header, follows goto/call and conditional
jump targets plus the fallthrough, and stops a path at goto, end and return. It is an upper
bound of the code an image can run, used to collect the images, sprites, flingies, sounds
and weapons an image can bring in.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

from ..formats import scripts

DEFS = Path(__file__).resolve().parents[2] / "defs" / "iscript"
WIDTHS = {"u8": 1, "s8": 1, "u16": 2, "pc": 2, "image": 2, "sprite": 2, "flingy": 2, "sound": 2, "weapon": 1}


class IscriptError(ValueError):
    """Bytecode that runs past the file."""


@dataclass(frozen=True)
class Table:
    opcodes: dict[int, tuple[str, tuple[tuple[str, str], ...]]]
    terminators: frozenset[str]
    header_animations: dict[int, int]
    raw: dict

    @classmethod
    def load(cls, profile_id: str = "bw-1.17.0.1") -> "Table":
        raw = json.loads((DEFS / f"{profile_id}.json").read_text())
        ops = {o["opcode"]: (o["name"], tuple(tuple(s.split(":")) for s in o["operands"])) for o in raw["opcodes"]}
        return cls(ops, frozenset(raw["terminators"]), {int(k): v for k, v in raw["header_animations"].items()}, raw)


@dataclass
class Closure:
    pcs: set[int] = field(default_factory=set)
    refs: dict[str, set[int]] = field(default_factory=lambda: {k: set() for k in ("image", "sprite", "flingy", "sound", "weapon")})
    opcodes: set[str] = field(default_factory=set)
    next_image: bool = False  # imgulnextid: the image after the running one
    gas_overlays: bool = False  # creategasoverlays: images 430.. / 435..
    sprol: bool = False  # sprol: sprite 505 can replace the operand for Goliath bullets


def entry_points(table: Table, data: bytes, header: int) -> list[int]:
    kind = data[header + 4]
    count = table.header_animations.get(kind)
    if count is None:
        raise IscriptError(f"header at {header:#x}: unknown type {kind}")
    return [p for p in struct.unpack_from(f"<{count}H", data, header + 8) if p]


def walk(table: Table, data: bytes, starts: list[int], c: Closure | None = None) -> Closure:
    c = c or Closure()
    work = list(starts)
    while work:
        pc = work.pop()
        while pc not in c.pcs:
            if pc >= len(data):
                raise IscriptError(f"PC {pc:#x} outside iscript.bin")
            c.pcs.add(pc)
            op = data[pc]
            name, operands = table.opcodes.get(op, ("nop", ()))  # 0x0c, 0x3e, 0x43, > 0x44: one-byte no-ops
            c.opcodes.add(name)
            at = pc + 1
            values = []
            for _, kind in operands:
                if kind == "sounds":
                    n = data[at]
                    ids = struct.unpack_from(f"<{n}H", data, at + 1)
                    c.refs["sound"].update(ids)
                    at += 1 + 2 * n
                    continue
                width = WIDTHS[kind]
                value = int.from_bytes(data[at : at + width], "little")
                values.append(value)
                if kind == "pc":
                    work.append(value)
                elif kind in c.refs:
                    c.refs[kind].add(value)
                at += width
            if name == "playsndbtwn":  # plays a + r mod (b - a + 1): every id in between
                c.refs["sound"].update(range(values[0], values[1] + 1))
            c.next_image |= name == "imgulnextid"
            c.gas_overlays |= name == "creategasoverlays"
            c.sprol |= name == "sprol"
            if name in table.terminators:
                break
            pc = at
    return c


def all_headers(data: bytes) -> dict[int, int]:
    return dict(scripts.iscript_headers(data))
