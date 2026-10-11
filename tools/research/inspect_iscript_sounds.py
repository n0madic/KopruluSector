#!/usr/bin/env python3
"""Collect every sound ID that IScript code reachable from any header can play.

Decodes the effective scripts\\iscript.bin with the pinned PyMS opcode table (parsed with
ast, never imported): operand sizes come from each PyMS operand type function's literal
size. Code is walked by control flow from every animation entry of every header; goto,
call/return and conditional jumps are followed. Reports the sound IDs used by playsnd,
playsndrand, playsndbtwn and attackmelee, plus checks for selected IDs. Reads archives only.
"""
import argparse
import ast
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inspect_resource_manifest import ARCHIVES, Storm  # noqa: E402

JUMPS = {"goto", "call", "randcondjmp", "pwrupcondjmp", "trgtrangecondjmp", "trgtarccondjmp",
         "curdirectcondjmp", "liftoffcondjmp"}
TERMINAL = {"goto", "end", "return"}
SOUND_OPS = {"playsnd", "playsndrand", "playsndbtwn", "attackmelee"}


def pyms_tables(path):
    tree = ast.parse(path.read_text())
    sizes, opcodes, entry_types = {}, None, None
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name.startswith("type_"):
            for sub in ast.walk(node):
                if isinstance(sub, ast.Return) and isinstance(sub.value, ast.Constant) and isinstance(sub.value.value, int):
                    sizes[node.name] = sub.value.value
                    break
        if isinstance(node, ast.Assign):
            names = [t.id for t in node.targets if isinstance(t, ast.Name)]
            if "OPCODES" in names:
                opcodes = [([n.value for n in e.elts[0].elts], [t.id for t in e.elts[1].elts]) for e in node.value.elts]
            if "ENTRY_TYPES" in names:
                entry_types = ast.literal_eval(node.value)
    return sizes, opcodes, entry_types


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", required=True, type=Path)
    ap.add_argument("--pyms", required=True, type=Path)
    ap.add_argument("--stormlib", default="/opt/homebrew/lib/libstorm.dylib")
    ap.add_argument("--check", default="", help="comma-separated sound IDs to report")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    sizes, opcodes, entry_types = pyms_tables(a.pyms / "PyMS/FileFormats/IScriptBIN.py")
    st = Storm(a.stormlib)
    arcs = [st.open(a.app / n) for n in ARCHIVES]
    data = None
    for h in arcs:
        data = st.read(h, "scripts\\iscript.bin")
        if data:
            break
    table = struct.unpack_from("<H", data, 0)[0]
    starts = []
    off = table
    while True:
        sid, hoff = struct.unpack_from("<HH", data, off)
        off += 4
        if sid == 0xFFFF:
            break
        assert data[hoff:hoff + 4] == b"SCPE", hex(hoff)
        n = entry_types[data[hoff + 4]]
        for p in struct.unpack_from(f"<{n}H", data, hoff + 8):
            if p:
                starts.append(p)
    seen, sounds, users = set(), {}, {}
    work = list(starts)
    while work:
        pc = work.pop()
        while pc not in seen:
            seen.add(pc)
            op = data[pc]
            names, types = opcodes[op]
            name = names[0]
            p = pc + 1
            vals = []
            count = None
            for t in types:
                if t == "type_sounds":
                    count = data[p]
                    p += 1
                    continue
                size = sizes[t]
                reps = count if (t == "type_soundid" and count is not None) else 1
                for _ in range(reps):
                    vals.append((t, int.from_bytes(data[p:p + size], "little")))
                    p += size
                count = None
            if name in SOUND_OPS:
                for t, v in vals:
                    if t == "type_soundid":
                        sounds[v] = sounds.get(v, 0) + 1
                        users.setdefault(v, []).append(hex(pc))
            for t, v in vals:
                if t == "type_label" and name in JUMPS:
                    work.append(v)
            if name in TERMINAL:
                break
            pc = p
    check = [int(x) for x in a.check.split(",") if x]
    report = {"headers_entry_points": len(starts), "instructions_visited": len(seen),
              "distinct_sounds": len(sounds), "sounds": {str(k): v for k, v in sorted(sounds.items())},
              "checked": {str(c): users.get(c, []) for c in check}}
    a.out.write_text(json.dumps(report, indent=1))
    print(json.dumps({k: report[k] for k in ("headers_entry_points", "instructions_visited", "distinct_sounds", "checked")}))


if __name__ == "__main__":
    main()
