"""Our own AI script code appended to an install's aiscript.bin: the assembler and the patch (phase M, the melee
opcodes no stock script reaches; docs/phase-multiplayer.md).

A source is lines of `label:`, `opcode operand ...` (the operands of defs/ai in order: integers, or labels for the
address operands; `debug`'s inline text after its target as a quoted string) and `stub ID label`. `#` starts a comment.
The code goes after the base file's last byte (its directory ends it), so its addresses are the base's size on; the
binary's addresses are WORDs, so it must end below 0x10000.

`stub ID label` puts in front of script ID's own code a stub that starts `label` beside it: `start_town`,
`multirun label`, `goto <ID's entry + 1>` (the entry's own start_town skipped: the probe's thread takes the town
multirun gives it, the parent's). ID's directory record then names the stub. The entry must be in aiscript.bin and start
with start_town (the melee scripts' do).

The patch (JSON, never Blizzard's bytes): {"name", "file", "size" and "crc32" of the base, "code" (hex), "redirect":
[[ID, offset], ...]}. apply() and Koprulu's kp_oracle (KP_AI_PATCH, a custom golden's "ai_patch") build the patched file
from it: the base checked by size and CRC-32, the code appended, the records' offsets replaced.
"""
from __future__ import annotations

import shlex
import struct
import zlib

from ..formats import scripts
from .ai import AiError, Table

FILE = "scripts\\aiscript.bin"
ADDRESS_LIMIT = 0x10000  # the interpreter reads its addresses as WORDs (0x493310)
OFFSET_FIELD = 4  # a directory record's offset after its id
STUB_TARGET = "start_town"


def _parse_int(text: str) -> int:
    return int(text, 0)


def _encode(width: int, value: int, what: str) -> bytes:
    if not 0 <= value < 1 << (8 * width):
        raise AiError(f"{what}: {value} does not fit {width} byte(s)")
    return value.to_bytes(width, "little")


def _records(base: bytes) -> dict[bytes, int]:
    """Each id's first record position in aiscript.bin's directory (the resolver's first match, 0x494d20)."""
    first: dict[bytes, int] = {}
    pos = struct.unpack_from("<I", base, 0)[0]
    for sid, _ in scripts.ai_directory(base, scripts.AI_RECORD):
        first.setdefault(sid, pos)
        pos += scripts.AI_RECORD
    return first


def assemble(source: str, base: bytes, name: str, table: Table | None = None) -> dict:
    """The patch of `source` over aiscript.bin's bytes `base`."""
    table = table or Table.load()
    by_name = {op.name: op for op in table.opcodes.values()}
    records = _records(base)
    origin = len(base)
    # Pass 1: each item's size and the labels' addresses; pass 2: the bytes.
    items: list[tuple[int, str, list[str]]] = []  # (line number, mnemonic, operands)
    labels: dict[str, int] = {}
    at = origin
    for number, line in enumerate(source.splitlines(), 1):
        words = shlex.split(line, comments=True, posix=True)
        while words and words[0].endswith(":"):
            label = words.pop(0)[:-1]
            if not label or label in labels:
                raise AiError(f"line {number}: label {label!r} empty or defined twice")
            labels[label] = at
        if not words:
            continue
        mnemonic, operands = words[0], words[1:]
        if mnemonic == "stub":
            if len(operands) != 2:
                raise AiError(f"line {number}: stub takes an id and a label")
            size = 1 + 3 + 3  # start_town, multirun label, goto entry + 1
        else:
            op = by_name.get(mnemonic)
            if op is None:
                raise AiError(f"line {number}: no opcode {mnemonic!r}")
            text = 1 if mnemonic == "debug" else 0
            if len(operands) != len(op.operands) + text:
                raise AiError(f"line {number}: {mnemonic} takes {len(op.operands) + text} operand(s)")
            size = 1 + sum(w for _, w in op.operands)
            if text:
                size += len(operands[-1].encode("latin-1")) + 1
        items.append((number, mnemonic, operands))
        at += size
    if at > ADDRESS_LIMIT:
        raise AiError(f"the code ends at {at:#x}, past the WORD addresses")

    def address(text: str, number: int) -> int:
        if text in labels:
            return labels[text]
        try:
            return _parse_int(text)
        except ValueError:
            raise AiError(f"line {number}: no label {text!r}") from None

    code = bytearray()
    redirect: list[list] = []
    for number, mnemonic, operands in items:
        here = origin + len(code)
        if mnemonic == "stub":
            sid = operands[0].encode("latin-1")
            if len(sid) != 4 or sid not in records:
                raise AiError(f"line {number}: no script {operands[0]!r} in aiscript.bin's directory")
            entry = struct.unpack_from("<I", base, records[sid] + OFFSET_FIELD)[0]
            if entry == 0 or base[entry] != by_name[STUB_TARGET].code:
                raise AiError(f"line {number}: {operands[0]}'s entry {entry:#x} does not start with {STUB_TARGET}")
            code += bytes([by_name[STUB_TARGET].code])
            code += bytes([by_name["multirun"].code]) + _encode(2, address(operands[1], number), "multirun")
            code += bytes([by_name["goto"].code]) + _encode(2, entry + 1, "goto")
            redirect.append([operands[0], here])
            continue
        op = by_name[mnemonic]
        code.append(op.code)
        for i, ((field, width), text) in enumerate(zip(op.operands, operands)):
            value = address(text, number) if i in op.targets else _parse_int(text)
            code += _encode(width, value, f"line {number} {mnemonic} {field}")
        if mnemonic == "debug":
            code += operands[-1].encode("latin-1") + b"\0"
    return {"name": name, "file": FILE, "size": len(base), "crc32": zlib.crc32(base), "code": code.hex(),
            "redirect": redirect}


def apply(base: bytes, patch: dict) -> bytes:
    """aiscript.bin patched: the base checked, the code appended, the stubs' records redirected."""
    if len(base) != patch["size"] or zlib.crc32(base) != patch["crc32"]:
        raise AiError(f"{patch['name']}: the base is not the aiscript.bin it was assembled over "
                      f"({len(base)} bytes, CRC-32 {zlib.crc32(base):#010x})")
    out = bytearray(base) + bytes.fromhex(patch["code"])
    records = _records(base)
    for sid, offset in patch["redirect"]:
        key = sid.encode("latin-1")
        if key not in records or not len(base) <= offset < len(out):
            raise AiError(f"{patch['name']}: cannot redirect {sid} to {offset:#x}")
        struct.pack_into("<I", out, records[key] + OFFSET_FIELD, offset)
    return bytes(out)
