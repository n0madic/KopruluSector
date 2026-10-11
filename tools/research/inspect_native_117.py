#!/usr/bin/env python3
"""Static witnesses for the reviewed 1.17 EXE; never loads or executes it."""

import argparse
import ast
import hashlib
import json
from pathlib import Path
import struct

import capstone
import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
SCHEMA_HASH = "b30ea0c1c2c4350d4b8de6c910eaf647d6c526a8a2efb64ba966e5c659a5125c"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    schema = args.schema.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown EXE hash: reviewed addresses do not apply")
    if hashlib.sha256(schema).hexdigest() != SCHEMA_HASH:
        parser.error("Unknown opcode schema hash: use the pinned PyMS AIBIN.py")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def disassemble(start, end):
        offset = pe.get_offset_from_rva(start - base)
        return [
            {
                "va": hex(ins.address),
                "rva": hex(ins.address - base),
                "file_offset": hex(pe.get_offset_from_rva(ins.address - base)),
                "bytes": ins.bytes.hex(),
                "mnemonic": ins.mnemonic,
                "operands": ins.op_str,
            }
            for ins in md.disasm(data[offset:offset + end - start], start)
        ]

    tree = ast.parse(schema)
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "AIBIN")
    labels = next(
        ast.literal_eval(n.value) for n in cls.body
        if isinstance(n, ast.Assign)
        and any(isinstance(t, ast.Name) and t.id == "short_labels" for t in n.targets)
    )
    table_offset = pe.get_offset_from_rva(0x494A98 - base)
    targets = struct.unpack_from("<113I", data, table_offset)
    blocks = {
        "script_scheduler": (0x4938E0, 0x49396B),
        "dispatch": (0x493A60, 0x493AA1),
        "read_byte": (0x4932F0, 0x49330D),
        "read_word": (0x493310, 0x493330),
        "goto": (0x493C5C, 0x493C6E),
        "wait": (0x494A1C, 0x494A30),
        "build": (0x493F54, 0x493F85),
        "wait_build_and_wait_buildstart": (0x493ABE, 0x493B51),
        "train": (0x493B73, 0x493BE6),
        "attack_add": (0x493EE6, 0x493F0A),
        "attack_prepare": (0x493F2E, 0x493F45),
        "attack_do": (0x493BE6, 0x493BFD),
        "attack_do_retry": (0x493AA9, 0x493ABE),
        "call": (0x4942FF, 0x49431B),
        "return": (0x49431B, 0x494332),
        "multirun": (0x494349, 0x494379),
        "random_jump": (0x493CC2, 0x493CF9),
    }
    native_ai = {
        "target_sha256": TARGET_HASH,
        "target_version": "1.17.0.1",
        "evidence": "static disassembly; opcode names projected from pinned PyMS; no runtime trace",
        "image_base": hex(base),
        "opcode_schema_sha256": SCHEMA_HASH,
        "dispatch_preferred_va": "0x493a60",
        "switch_table_preferred_va": "0x494a98",
        "opcode_max": 0x70,
        "handlers": [
            {"opcode": i, "name_from_schema": labels[i], "va": hex(va), "rva": hex(va - base)}
            for i, va in enumerate(targets)
        ],
        "witnesses": {name: disassemble(*bounds) for name, bounds in blocks.items()},
    }
    vectors = []
    for seed in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
        state = seed
        values = []
        for _ in range(5):
            state = (state * 22695477 + 1) & 0xFFFFFFFF
            values.append({"state": state, "result": (state >> 16) & 0x7FFF})
        vectors.append({"seed": seed, "values": values})
    prng = {
        "target_sha256": TARGET_HASH,
        "target_version": "1.17.0.1",
        "image_base": hex(base),
        "evidence": "static disassembly only; functions not executed",
        "state_preferred_va": "0x67393c",
        "recurrence": "state = (22695477 * state + 1) mod 2^32; result = (state >> 16) & 0x7fff",
        "functions": [
            {"preferred_va_start": hex(lo), "instructions": disassemble(lo, hi)}
            for lo, hi in ((0x478200, 0x478232), (0x478270, 0x4782C4))
        ],
        "derived_vectors": vectors,
    }
    args.out.mkdir(parents=True, exist_ok=True)
    for name, result in (("native-ai-static.json", native_ai), ("prng-static.json", prng)):
        (args.out / name).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"target_sha256": TARGET_HASH, "opcode_handlers": len(targets), "output": str(args.out)}))


if __name__ == "__main__":
    main()
