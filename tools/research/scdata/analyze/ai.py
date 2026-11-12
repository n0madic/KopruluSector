"""AI script decoding and the conservative reachable closure (plan §5.2).

Operand widths are the native reads of every dispatcher ordinal (defs/ai/<profile>.json).
The closure follows every address operand (`target`, `*_target`) and the fallthrough;
`call` and `multirun` keep both their target and their continuation. Local fallthrough
ends after goto, stop, return, kill_thread, fatal_error and debug (docs/research/campaigns.md).
It is an upper bound: it includes impossible states and random branches, and it is not
evidence that a branch runs.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

from ..errors import ConverterError

DEFS = Path(__file__).resolve().parents[4] / "defs" / "ai"
WIDTHS = {"u8": 1, "u16le": 2, "u32le": 4}
AISCRIPT, BWSCRIPT = 0, 1


class AiError(ConverterError, ValueError):
    """Bytecode the native dispatcher cannot decode (unknown opcode or truncated operand)."""


@dataclass(frozen=True)
class Opcode:
    code: int
    name: str
    operands: tuple[tuple[str, int], ...]  # (name, width)

    @property
    def targets(self) -> tuple[int, ...]:
        """Indexes of address operands."""
        return tuple(i for i, (n, _) in enumerate(self.operands) if n == "target" or n.endswith("_target"))


@dataclass(frozen=True)
class Table:
    opcodes: dict[int, Opcode]
    terminators: frozenset[str]

    @classmethod
    def load(cls, profile_id: str = "bw-1.17.0.1") -> "Table":
        raw = json.loads((DEFS / f"{profile_id}.json").read_text())
        ops = {}
        for o in raw["opcodes"]:
            operands = tuple((spec.split(":")[0], WIDTHS[spec.split(":")[1]]) for spec in o["operands"])
            ops[o["opcode"]] = Opcode(o["opcode"], o["name"], operands)
        return cls(ops, frozenset(raw["terminators"]))


@dataclass(frozen=True)
class Instruction:
    pc: int
    opcode: Opcode
    values: tuple[int, ...]
    next_pc: int


def decode(table: Table, data: bytes, pc: int) -> Instruction:
    if pc >= len(data):
        raise AiError(f"PC {pc:#x} outside the script file")
    op = table.opcodes.get(data[pc])
    if op is None:
        raise AiError(f"opcode {data[pc]:#x} at {pc:#x} is beyond the native dispatcher")
    at, values = pc + 1, []
    for _, width in op.operands:
        if at + width > len(data):
            raise AiError(f"{op.name} at {pc:#x}: operand runs past the end")
        values.append(int.from_bytes(data[at : at + width], "little"))
        at += width
    return Instruction(pc, op, tuple(values), at)


@dataclass
class Closure:
    """Reachable instructions per binary, keyed by PC."""
    instructions: dict[tuple[int, int], Instruction] = field(default_factory=dict)

    def opcode_names(self) -> set[str]:
        return {i.opcode.name for i in self.instructions.values()}

    def operand_values(self, name: str) -> set[int]:
        """Every value of operands called `name` (e.g. 'unit', 'upgrade', 'technology')."""
        out = set()
        for ins in self.instructions.values():
            for (n, _), v in zip(ins.opcode.operands, ins.values):
                if n == name:
                    out.add(v)
        return out


def closure(table: Table, bins: tuple[bytes, bytes], roots: list[tuple[int, int]]) -> Closure:
    """`roots` are (bin selector, PC) pairs; jumps stay in their binary."""
    c = Closure()
    work = list(roots)
    while work:
        selector, pc = work.pop()
        data = bins[selector]
        while (selector, pc) not in c.instructions:
            ins = decode(table, data, pc)
            c.instructions[selector, pc] = ins
            for i in ins.opcode.targets:
                work.append((selector, ins.values[i]))
            if ins.opcode.name in table.terminators:
                break
            pc = ins.next_pc
    return c


def resolve(aiscript: bytes, bwscript: bytes, script_id: bytes) -> tuple[int, int] | None:
    """`0x494d20`: (bin selector, offset) of a four-byte script id, or None."""
    from ..formats import scripts

    found = scripts.resolve_ai(aiscript, bwscript, script_id)
    return None if found is None else (found[1], found[0])


def script_id(value: int) -> bytes:
    """A trigger's u32 AI script field as the four-byte id the resolver compares."""
    return struct.pack("<I", value)
